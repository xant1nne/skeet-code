#ifndef SKEET_GGUF_H
#define SKEET_GGUF_H

#include <stdint.h>
#include <stddef.h>

typedef struct GgufMeta {
    char architecture[64];
    uint32_t block_count;
    uint32_t context_length;
    uint32_t embedding_length;
    uint32_t head_count;
    uint32_t head_count_kv;
    uint32_t expert_count;
    uint32_t expert_used_count;
    uint32_t file_type;
    uint64_t tensor_count;
    uint64_t file_size_bytes;
} GgufMeta;

int gguf_read_meta(const char *path, GgufMeta *out);

#endif
