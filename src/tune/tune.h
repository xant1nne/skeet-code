#ifndef SKEET_TUNE_H
#define SKEET_TUNE_H

#include "tune/hw.h"
#include "tune/gguf.h"
#include <stdint.h>

typedef struct TuneParams {
    int n_gpu_layers;
    int threads;
    int ctx_size;
    int flash_attention;
    char cache_type_k[16];
    char cache_type_v[16];
    int moe_cpu_experts;
    char flags_str[512];
} TuneParams;

int tune_calc(const GgufMeta *meta, const HwInfo *hw, const char *manual_flags, TuneParams *out);
int tune_model(const char *model_path, const char *manual_flags, TuneParams *out, GgufMeta *out_meta, HwInfo *out_hw);
int tune_cache_get(const char *model_path, uint64_t vram_bytes, TuneParams *out);
int tune_cache_set(const char *model_path, uint64_t vram_bytes, const TuneParams *params);

#endif
