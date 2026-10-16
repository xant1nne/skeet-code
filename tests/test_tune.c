#include "tune/gguf.h"
#include "tune/hw.h"
#include "tune/tune.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void write_gguf_str(FILE *f, const char *s) {
    uint64_t len = strlen(s);
    fwrite(&len, sizeof(len), 1, f);
    fwrite(s, 1, len, f);
}

static void create_test_gguf(const char *path) {
    FILE *f = fopen(path, "wb");
    assert(f != NULL);

    uint32_t magic = 0x46554747;
    uint32_t version = 3;
    uint64_t tensor_count = 290;
    uint64_t kv_count = 6;

    fwrite(&magic, sizeof(magic), 1, f);
    fwrite(&version, sizeof(version), 1, f);
    fwrite(&tensor_count, sizeof(tensor_count), 1, f);
    fwrite(&kv_count, sizeof(kv_count), 1, f);

    /* 1. general.architecture = "llama" */
    write_gguf_str(f, "general.architecture");
    uint32_t type_str = 8;
    fwrite(&type_str, sizeof(type_str), 1, f);
    write_gguf_str(f, "llama");

    /* 2. llama.block_count = 32 */
    write_gguf_str(f, "llama.block_count");
    uint32_t type_u32 = 4;
    fwrite(&type_u32, sizeof(type_u32), 1, f);
    uint32_t blocks = 32;
    fwrite(&blocks, sizeof(blocks), 1, f);

    /* 3. llama.context_length = 8192 */
    write_gguf_str(f, "llama.context_length");
    fwrite(&type_u32, sizeof(type_u32), 1, f);
    uint32_t ctx_len = 8192;
    fwrite(&ctx_len, sizeof(ctx_len), 1, f);

    /* 4. llama.embedding_length = 4096 */
    write_gguf_str(f, "llama.embedding_length");
    fwrite(&type_u32, sizeof(type_u32), 1, f);
    uint32_t emb = 4096;
    fwrite(&emb, sizeof(emb), 1, f);

    /* 5. llama.attention.head_count = 32 */
    write_gguf_str(f, "llama.attention.head_count");
    fwrite(&type_u32, sizeof(type_u32), 1, f);
    uint32_t heads = 32;
    fwrite(&heads, sizeof(heads), 1, f);

    /* 6. llama.attention.head_count_kv = 8 */
    write_gguf_str(f, "llama.attention.head_count_kv");
    fwrite(&type_u32, sizeof(type_u32), 1, f);
    uint32_t heads_kv = 8;
    fwrite(&heads_kv, sizeof(heads_kv), 1, f);

    fclose(f);
}

int main(void) {
    const char *tmp_model = "/tmp/test_tune_synth.gguf";
    unlink(tmp_model);
    create_test_gguf(tmp_model);

    GgufMeta meta;
    int rc = gguf_read_meta(tmp_model, &meta);
    assert(rc == 0);
    assert(strcmp(meta.architecture, "llama") == 0);
    assert(meta.block_count == 32);
    assert(meta.context_length == 8192);
    assert(meta.embedding_length == 4096);
    assert(meta.head_count == 32);
    assert(meta.head_count_kv == 8);

    /* Test Case 1: CPU only (no GPU) */
    HwInfo hw_cpu;
    memset(&hw_cpu, 0, sizeof(hw_cpu));
    hw_cpu.ram_total_bytes = 32ULL * 1024 * 1024 * 1024;
    hw_cpu.ram_free_bytes = 16ULL * 1024 * 1024 * 1024;
    hw_cpu.cpu_physical_cores = 8;
    hw_cpu.cpu_logical_cores = 16;
    hw_cpu.has_gpu = 0;

    TuneParams tp_cpu;
    rc = tune_calc(&meta, &hw_cpu, NULL, &tp_cpu);
    assert(rc == 0);
    assert(tp_cpu.n_gpu_layers == 0);
    assert(tp_cpu.threads == 8);
    assert(tp_cpu.ctx_size == 8192);
    assert(tp_cpu.flash_attention == 0);
    assert(strstr(tp_cpu.flags_str, "-ngl 0") != NULL);
    assert(strstr(tp_cpu.flags_str, "-t 8") != NULL);
    assert(strstr(tp_cpu.flags_str, "-c 8192") != NULL);

    /* Test Case 2: GPU with plenty of VRAM (24 GB) */
    HwInfo hw_gpu_large;
    memset(&hw_gpu_large, 0, sizeof(hw_gpu_large));
    hw_gpu_large.ram_total_bytes = 64ULL * 1024 * 1024 * 1024;
    hw_gpu_large.cpu_physical_cores = 12;
    hw_gpu_large.has_gpu = 1;
    hw_gpu_large.vram_total_bytes = 24ULL * 1024 * 1024 * 1024;
    hw_gpu_large.vram_free_bytes = 22ULL * 1024 * 1024 * 1024;
    meta.file_size_bytes = 4ULL * 1024 * 1024 * 1024;

    TuneParams tp_gpu;
    rc = tune_calc(&meta, &hw_gpu_large, NULL, &tp_gpu);
    assert(rc == 0);
    assert(tp_gpu.n_gpu_layers >= 32);
    assert(tp_gpu.threads == 12);
    assert(tp_gpu.flash_attention == 1);
    assert(strstr(tp_gpu.flags_str, "-fa") != NULL);

    /* Test Case 3: GPU with constrained VRAM (2 GB) */
    HwInfo hw_gpu_small;
    memset(&hw_gpu_small, 0, sizeof(hw_gpu_small));
    hw_gpu_small.ram_total_bytes = 16ULL * 1024 * 1024 * 1024;
    hw_gpu_small.cpu_physical_cores = 6;
    hw_gpu_small.has_gpu = 1;
    hw_gpu_small.vram_total_bytes = 4ULL * 1024 * 1024 * 1024;
    hw_gpu_small.vram_free_bytes = 2ULL * 1024 * 1024 * 1024;

    TuneParams tp_small;
    rc = tune_calc(&meta, &hw_gpu_small, NULL, &tp_small);
    assert(rc == 0);
    assert(tp_small.n_gpu_layers <= 32);
    if (tp_small.n_gpu_layers > 0) {
        assert(strcmp(tp_small.cache_type_k, "q4_0") == 0 || strcmp(tp_small.cache_type_k, "q8_0") == 0);
    }

    /* Test Case 4: Cache get / set */
    rc = tune_cache_set(tmp_model, hw_gpu_large.vram_total_bytes, &tp_gpu);
    assert(rc == 0);

    TuneParams tp_cached;
    memset(&tp_cached, 0, sizeof(tp_cached));
    rc = tune_cache_get(tmp_model, hw_gpu_large.vram_total_bytes, &tp_cached);
    assert(rc == 0);
    assert(tp_cached.n_gpu_layers == tp_gpu.n_gpu_layers);
    assert(tp_cached.threads == tp_gpu.threads);
    assert(tp_cached.ctx_size == tp_gpu.ctx_size);
    assert(strcmp(tp_cached.flags_str, tp_gpu.flags_str) == 0);

    unlink(tmp_model);
    printf("test_tune: PASS\n");
    return 0;
}
