#include "tune/gguf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GGUF_MAGIC 0x46554747

enum {
    GGUF_TYPE_UINT8   = 0,
    GGUF_TYPE_INT8    = 1,
    GGUF_TYPE_UINT16  = 2,
    GGUF_TYPE_INT16   = 3,
    GGUF_TYPE_UINT32  = 4,
    GGUF_TYPE_INT32   = 5,
    GGUF_TYPE_FLOAT32 = 6,
    GGUF_TYPE_BOOL    = 7,
    GGUF_TYPE_STRING  = 8,
    GGUF_TYPE_ARRAY   = 9,
    GGUF_TYPE_UINT64  = 10,
    GGUF_TYPE_INT64   = 11,
    GGUF_TYPE_FLOAT64 = 12
};

static int read_str(FILE *f, char *buf, size_t buf_sz) {
    uint64_t len = 0;
    if (fread(&len, sizeof(len), 1, f) != 1) {
        return -1;
    }
    if (len >= 65536) {
        return -1;
    }
    size_t to_read = len < buf_sz - 1 ? (size_t)len : buf_sz - 1;
    if (to_read > 0) {
        if (fread(buf, 1, to_read, f) != to_read) {
            return -1;
        }
    }
    buf[to_read] = '\0';
    if (len > to_read) {
        fseek(f, (long)(len - to_read), SEEK_CUR);
    }
    return 0;
}

static int skip_val(FILE *f, uint32_t val_type);

static int skip_val(FILE *f, uint32_t val_type) {
    switch (val_type) {
    case GGUF_TYPE_UINT8:
    case GGUF_TYPE_INT8:
    case GGUF_TYPE_BOOL:
        return fseek(f, 1, SEEK_CUR);
    case GGUF_TYPE_UINT16:
    case GGUF_TYPE_INT16:
        return fseek(f, 2, SEEK_CUR);
    case GGUF_TYPE_UINT32:
    case GGUF_TYPE_INT32:
    case GGUF_TYPE_FLOAT32:
        return fseek(f, 4, SEEK_CUR);
    case GGUF_TYPE_UINT64:
    case GGUF_TYPE_INT64:
    case GGUF_TYPE_FLOAT64:
        return fseek(f, 8, SEEK_CUR);
    case GGUF_TYPE_STRING: {
        uint64_t len = 0;
        if (fread(&len, sizeof(len), 1, f) != 1) {
            return -1;
        }
        return fseek(f, (long)len, SEEK_CUR);
    }
    case GGUF_TYPE_ARRAY: {
        uint32_t elem_type = 0;
        uint64_t count = 0;
        if (fread(&elem_type, sizeof(elem_type), 1, f) != 1) {
            return -1;
        }
        if (fread(&count, sizeof(count), 1, f) != 1) {
            return -1;
        }
        for (uint64_t i = 0; i < count; i++) {
            if (skip_val(f, elem_type) != 0) {
                return -1;
            }
        }
        return 0;
    }
    default:
        return -1;
    }
}

int gguf_read_meta(const char *path, GgufMeta *out) {
    if (!path || !out) {
        return -1;
    }
    memset(out, 0, sizeof(*out));

    FILE *f = fopen(path, "rb");
    if (!f) {
        return -1;
    }

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    out->file_size_bytes = fsize > 0 ? (uint64_t)fsize : 0;

    uint32_t magic = 0;
    if (fread(&magic, sizeof(magic), 1, f) != 1 || magic != GGUF_MAGIC) {
        fclose(f);
        return -1;
    }

    uint32_t version = 0;
    if (fread(&version, sizeof(version), 1, f) != 1 || version < 1) {
        fclose(f);
        return -1;
    }

    uint64_t tensor_count = 0;
    uint64_t kv_count = 0;
    if (fread(&tensor_count, sizeof(tensor_count), 1, f) != 1 ||
        fread(&kv_count, sizeof(kv_count), 1, f) != 1) {
        fclose(f);
        return -1;
    }

    out->tensor_count = tensor_count;

    for (uint64_t i = 0; i < kv_count; i++) {
        char key[256];
        if (read_str(f, key, sizeof(key)) != 0) {
            break;
        }

        uint32_t val_type = 0;
        if (fread(&val_type, sizeof(val_type), 1, f) != 1) {
            break;
        }

        if (strcmp(key, "general.architecture") == 0 && val_type == GGUF_TYPE_STRING) {
            read_str(f, out->architecture, sizeof(out->architecture));
        } else if (strcmp(key, "general.file_type") == 0 && (val_type == GGUF_TYPE_UINT32 || val_type == GGUF_TYPE_INT32)) {
            fread(&out->file_type, 4, 1, f);
        } else if (strstr(key, ".block_count") != NULL) {
            if (val_type == GGUF_TYPE_UINT32 || val_type == GGUF_TYPE_INT32) {
                fread(&out->block_count, 4, 1, f);
            } else if (val_type == GGUF_TYPE_UINT64 || val_type == GGUF_TYPE_INT64) {
                uint64_t v = 0;
                fread(&v, 8, 1, f);
                out->block_count = (uint32_t)v;
            } else {
                skip_val(f, val_type);
            }
        } else if (strstr(key, ".context_length") != NULL) {
            if (val_type == GGUF_TYPE_UINT32 || val_type == GGUF_TYPE_INT32) {
                fread(&out->context_length, 4, 1, f);
            } else if (val_type == GGUF_TYPE_UINT64 || val_type == GGUF_TYPE_INT64) {
                uint64_t v = 0;
                fread(&v, 8, 1, f);
                out->context_length = (uint32_t)v;
            } else {
                skip_val(f, val_type);
            }
        } else if (strstr(key, ".embedding_length") != NULL) {
            if (val_type == GGUF_TYPE_UINT32 || val_type == GGUF_TYPE_INT32) {
                fread(&out->embedding_length, 4, 1, f);
            } else {
                skip_val(f, val_type);
            }
        } else if (strstr(key, ".attention.head_count_kv") != NULL) {
            if (val_type == GGUF_TYPE_UINT32 || val_type == GGUF_TYPE_INT32) {
                fread(&out->head_count_kv, 4, 1, f);
            } else {
                skip_val(f, val_type);
            }
        } else if (strstr(key, ".attention.head_count") != NULL) {
            if (val_type == GGUF_TYPE_UINT32 || val_type == GGUF_TYPE_INT32) {
                fread(&out->head_count, 4, 1, f);
            } else {
                skip_val(f, val_type);
            }
        } else if (strstr(key, ".expert_count") != NULL) {
            if (val_type == GGUF_TYPE_UINT32 || val_type == GGUF_TYPE_INT32) {
                fread(&out->expert_count, 4, 1, f);
            } else {
                skip_val(f, val_type);
            }
        } else if (strstr(key, ".expert_used_count") != NULL) {
            if (val_type == GGUF_TYPE_UINT32 || val_type == GGUF_TYPE_INT32) {
                fread(&out->expert_used_count, 4, 1, f);
            } else {
                skip_val(f, val_type);
            }
        } else {
            skip_val(f, val_type);
        }
    }

    fclose(f);
    return 0;
}
