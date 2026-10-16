#include "tune/tune.h"
#include "cJSON.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static uint64_t hash_key(const char *path, uint64_t vram) {
    uint64_t h = 14695981039346656037ULL;
    if (path) {
        while (*path) {
            h ^= (uint8_t)*path++;
            h *= 1099511628211ULL;
        }
    }
    for (int i = 0; i < 8; i++) {
        h ^= (uint8_t)((vram >> (i * 8)) & 0xFF);
        h *= 1099511628211ULL;
    }
    return h;
}

static int get_cache_path(char *buf, size_t sz) {
    const char *xdg = getenv("XDG_CACHE_HOME");
    const char *home = getenv("HOME");
    int n;
    if (xdg && xdg[0]) {
        n = snprintf(buf, sz, "%s/skeet/tune.json", xdg);
    } else if (home && home[0]) {
        n = snprintf(buf, sz, "%s/.cache/skeet/tune.json", home);
    } else {
        return -1;
    }
    return (n > 0 && (size_t)n < sz) ? 0 : -1;
}

static int mkdir_p(const char *path) {
    size_t len = strlen(path);
    if (len == 0 || len >= 4096) {
        return -1;
    }
    char tmp[4096];
    memcpy(tmp, path, len + 1);
    for (size_t i = 1; i < len; i++) {
        if (tmp[i] != '/') {
            continue;
        }
        tmp[i] = '\0';
        if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
            return -1;
        }
        tmp[i] = '/';
    }
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
        return -1;
    }
    return 0;
}

int tune_calc(const GgufMeta *meta, const HwInfo *hw, const char *manual_flags, TuneParams *out) {
    if (!meta || !hw || !out) {
        return -1;
    }
    memset(out, 0, sizeof(*out));

    int cores = hw->cpu_physical_cores > 0 ? hw->cpu_physical_cores : 4;
    out->threads = cores > 16 ? 16 : cores;
    if (out->threads < 1) {
        out->threads = 1;
    }

    uint32_t ctx = meta->context_length > 0 ? meta->context_length : 4096;
    if (ctx > 16384) {
        ctx = 16384;
    }
    if (hw->ram_total_bytes > 0 && hw->ram_total_bytes < 8ULL * 1024 * 1024 * 1024 && ctx > 4096) {
        ctx = 4096;
    }
    out->ctx_size = (int)ctx;

    strcpy(out->cache_type_k, "f16");
    strcpy(out->cache_type_v, "f16");

    uint32_t L = meta->block_count > 0 ? meta->block_count : 32;
    uint32_t head_count = meta->head_count > 0 ? meta->head_count : 32;
    uint32_t head_count_kv = meta->head_count_kv > 0 ? meta->head_count_kv : head_count;
    uint32_t embedding = meta->embedding_length > 0 ? meta->embedding_length : 4096;
    uint32_t head_dim = head_count > 0 ? (embedding / head_count) : 128;
    if (head_dim == 0) {
        head_dim = 128;
    }

    uint64_t kv_full_bytes = 2ULL * (uint64_t)L * head_count_kv * head_dim * 2 * out->ctx_size;

    if (!hw->has_gpu || hw->vram_total_bytes == 0) {
        out->n_gpu_layers = 0;
    } else {
        uint64_t reserve = 512ULL * 1024 * 1024;
        uint64_t free_vram = hw->vram_free_bytes > 0 ? hw->vram_free_bytes : hw->vram_total_bytes;
        uint64_t avail_vram = free_vram > reserve ? (free_vram - reserve) : 0;

        uint64_t file_sz = meta->file_size_bytes > 0 ? meta->file_size_bytes : 0;
        uint64_t layer_weights = (file_sz > 0 && L > 0) ? ((file_sz * 9) / (10 * L)) : 0;
        uint64_t base_weights = file_sz > (layer_weights * L) ? (file_sz - layer_weights * L) : 0;

        if (avail_vram >= file_sz + kv_full_bytes) {
            out->n_gpu_layers = (int)L + 1;
        } else if (avail_vram > base_weights && L > 0) {
            uint64_t kv_layer = kv_full_bytes / L;
            uint64_t per_layer_vram = layer_weights + kv_layer;
            if (per_layer_vram > 0) {
                out->n_gpu_layers = (int)((avail_vram - base_weights) / per_layer_vram);
            }
            if (out->n_gpu_layers > (int)L) {
                out->n_gpu_layers = (int)L;
            }
            if (out->n_gpu_layers < 0) {
                out->n_gpu_layers = 0;
            }
        } else {
            out->n_gpu_layers = 0;
        }

        if (out->n_gpu_layers > 0 && out->n_gpu_layers <= (int)L) {
            if (avail_vram < 2ULL * 1024 * 1024 * 1024) {
                strcpy(out->cache_type_k, "q4_0");
                strcpy(out->cache_type_v, "q4_0");
            } else if (avail_vram < 4ULL * 1024 * 1024 * 1024) {
                strcpy(out->cache_type_k, "q8_0");
                strcpy(out->cache_type_v, "q8_0");
            }
        }

        if (meta->architecture[0]) {
            const char *arch = meta->architecture;
            if (strstr(arch, "llama") || strstr(arch, "mistral") || strstr(arch, "qwen") ||
                strstr(arch, "deepseek") || strstr(arch, "phi") || strstr(arch, "gemma")) {
                out->flash_attention = 1;
            }
        } else {
            out->flash_attention = 1;
        }

        if (meta->expert_count > 0 && out->n_gpu_layers < (int)L) {
            if (meta->expert_count > meta->expert_used_count) {
                out->moe_cpu_experts = (int)(meta->expert_count - meta->expert_used_count);
            }
        }
    }

    char flags[512];
    int pos = snprintf(flags, sizeof(flags), "-ngl %d -t %d -c %d", out->n_gpu_layers, out->threads, out->ctx_size);
    if (out->flash_attention && pos < (int)sizeof(flags) - 4) {
        pos += snprintf(flags + pos, sizeof(flags) - (size_t)pos, " -fa");
    }
    if (strcmp(out->cache_type_k, "f16") != 0 && pos < (int)sizeof(flags) - 32) {
        pos += snprintf(flags + pos, sizeof(flags) - (size_t)pos, " -ctk %s -ctv %s", out->cache_type_k, out->cache_type_v);
    }
    if (manual_flags && manual_flags[0] && pos < (int)sizeof(flags) - (int)strlen(manual_flags) - 2) {
        snprintf(flags + pos, sizeof(flags) - (size_t)pos, " %s", manual_flags);
    }
    strncpy(out->flags_str, flags, sizeof(out->flags_str) - 1);
    out->flags_str[sizeof(out->flags_str) - 1] = '\0';

    return 0;
}

int tune_cache_get(const char *model_path, uint64_t vram_bytes, TuneParams *out) {
    if (!model_path || !out) {
        return -1;
    }
    char path[1024];
    if (get_cache_path(path, sizeof(path)) != 0) {
        return -1;
    }

    FILE *f = fopen(path, "rb");
    if (!f) {
        return -1;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 10 * 1024 * 1024) {
        fclose(f);
        return -1;
    }

    char *buf = malloc((size_t)sz + 1);
    if (!buf) {
        fclose(f);
        return -1;
    }
    size_t nr = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[nr] = '\0';

    cJSON *root = cJSON_ParseWithLength(buf, nr);
    free(buf);
    if (!root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return -1;
    }

    char hex[32];
    snprintf(hex, sizeof(hex), "%016llx", (unsigned long long)hash_key(model_path, vram_bytes));
    cJSON *entry = cJSON_GetObjectItemCaseSensitive(root, hex);
    if (!entry || !cJSON_IsObject(entry)) {
        cJSON_Delete(root);
        return -1;
    }

    cJSON *ngl = cJSON_GetObjectItemCaseSensitive(entry, "n_gpu_layers");
    cJSON *th = cJSON_GetObjectItemCaseSensitive(entry, "threads");
    cJSON *ctx = cJSON_GetObjectItemCaseSensitive(entry, "ctx_size");
    cJSON *fa = cJSON_GetObjectItemCaseSensitive(entry, "flash_attention");
    cJSON *ctk = cJSON_GetObjectItemCaseSensitive(entry, "cache_type_k");
    cJSON *ctv = cJSON_GetObjectItemCaseSensitive(entry, "cache_type_v");
    cJSON *flg = cJSON_GetObjectItemCaseSensitive(entry, "flags");

    if (cJSON_IsNumber(ngl)) out->n_gpu_layers = ngl->valueint;
    if (cJSON_IsNumber(th)) out->threads = th->valueint;
    if (cJSON_IsNumber(ctx)) out->ctx_size = ctx->valueint;
    if (cJSON_IsNumber(fa)) out->flash_attention = fa->valueint;
    if (cJSON_IsString(ctk) && ctk->valuestring) {
        strncpy(out->cache_type_k, ctk->valuestring, sizeof(out->cache_type_k) - 1);
    }
    if (cJSON_IsString(ctv) && ctv->valuestring) {
        strncpy(out->cache_type_v, ctv->valuestring, sizeof(out->cache_type_v) - 1);
    }
    if (cJSON_IsString(flg) && flg->valuestring) {
        strncpy(out->flags_str, flg->valuestring, sizeof(out->flags_str) - 1);
    }

    cJSON_Delete(root);
    return 0;
}

int tune_cache_set(const char *model_path, uint64_t vram_bytes, const TuneParams *params) {
    if (!model_path || !params) {
        return -1;
    }
    char path[1024];
    if (get_cache_path(path, sizeof(path)) != 0) {
        return -1;
    }

    char dir[1024];
    strncpy(dir, path, sizeof(dir) - 1);
    dir[sizeof(dir) - 1] = '\0';
    char *slash = strrchr(dir, '/');
    if (slash) {
        *slash = '\0';
        mkdir_p(dir);
    }

    cJSON *root = NULL;
    FILE *f = fopen(path, "rb");
    if (f) {
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (sz > 0 && sz < 10 * 1024 * 1024) {
            char *buf = malloc((size_t)sz + 1);
            if (buf) {
                size_t nr = fread(buf, 1, (size_t)sz, f);
                buf[nr] = '\0';
                root = cJSON_ParseWithLength(buf, nr);
                free(buf);
            }
        }
        fclose(f);
    }

    if (!root || !cJSON_IsObject(root)) {
        if (root) cJSON_Delete(root);
        root = cJSON_CreateObject();
    }
    if (!root) {
        return -1;
    }

    char hex[32];
    snprintf(hex, sizeof(hex), "%016llx", (unsigned long long)hash_key(model_path, vram_bytes));

    cJSON *entry = cJSON_CreateObject();
    if (!entry) {
        cJSON_Delete(root);
        return -1;
    }

    cJSON_AddStringToObject(entry, "model_path", model_path);
    cJSON_AddNumberToObject(entry, "vram_bytes", (double)vram_bytes);
    cJSON_AddNumberToObject(entry, "n_gpu_layers", params->n_gpu_layers);
    cJSON_AddNumberToObject(entry, "threads", params->threads);
    cJSON_AddNumberToObject(entry, "ctx_size", params->ctx_size);
    cJSON_AddNumberToObject(entry, "flash_attention", params->flash_attention);
    cJSON_AddStringToObject(entry, "cache_type_k", params->cache_type_k);
    cJSON_AddStringToObject(entry, "cache_type_v", params->cache_type_v);
    cJSON_AddStringToObject(entry, "flags", params->flags_str);

    cJSON_DeleteItemFromObjectCaseSensitive(root, hex);
    cJSON_AddItemToObject(root, hex, entry);

    char *json_txt = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json_txt) {
        return -1;
    }

    char tmp_path[1050];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp.%d", path, (int)getpid());
    FILE *out_f = fopen(tmp_path, "wb");
    if (!out_f) {
        free(json_txt);
        return -1;
    }
    size_t len = strlen(json_txt);
    size_t nw = fwrite(json_txt, 1, len, out_f);
    fclose(out_f);
    free(json_txt);

    if (nw != len) {
        unlink(tmp_path);
        return -1;
    }
    if (rename(tmp_path, path) != 0) {
        unlink(tmp_path);
        return -1;
    }

    return 0;
}

int tune_model(const char *model_path, const char *manual_flags, TuneParams *out, GgufMeta *out_meta, HwInfo *out_hw) {
    if (!out) {
        return -1;
    }

    HwInfo hw;
    if (hw_detect(&hw) != 0) {
        return -1;
    }
    if (out_hw) {
        *out_hw = hw;
    }

    if (!model_path || !model_path[0]) {
        GgufMeta dummy;
        memset(&dummy, 0, sizeof(dummy));
        dummy.block_count = 32;
        dummy.context_length = 4096;
        return tune_calc(&dummy, &hw, manual_flags, out);
    }

    if (!manual_flags || !manual_flags[0]) {
        if (tune_cache_get(model_path, hw.vram_total_bytes, out) == 0) {
            if (out_meta) {
                gguf_read_meta(model_path, out_meta);
            }
            return 0;
        }
    }

    GgufMeta meta;
    if (gguf_read_meta(model_path, &meta) != 0) {
        return -1;
    }
    if (out_meta) {
        *out_meta = meta;
    }

    if (tune_calc(&meta, &hw, manual_flags, out) != 0) {
        return -1;
    }

    tune_cache_set(model_path, hw.vram_total_bytes, out);
    return 0;
}
