#ifndef SKEET_HW_H
#define SKEET_HW_H

#include <stdint.h>
#include <stddef.h>

typedef struct HwInfo {
    uint64_t ram_total_bytes;
    uint64_t ram_free_bytes;
    int cpu_physical_cores;
    int cpu_logical_cores;
    uint64_t vram_total_bytes;
    uint64_t vram_free_bytes;
    char gpu_name[128];
    int has_gpu;
} HwInfo;

int hw_detect(HwInfo *out);

#endif
