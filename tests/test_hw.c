#include "tune/hw.h"

#include <assert.h>
#include <stdio.h>

int main(void) {
    HwInfo hw;
    int rc = hw_detect(&hw);
    assert(rc == 0);

    assert(hw.ram_total_bytes > 0);
    assert(hw.cpu_physical_cores >= 1);
    assert(hw.cpu_logical_cores >= 1);
    assert(hw.cpu_logical_cores >= hw.cpu_physical_cores);

    printf("test_hw: RAM total=%lu MB, free=%lu MB, CPU physical=%d, logical=%d, GPU=%s, VRAM total=%lu MB, free=%lu MB\n",
           (unsigned long)(hw.ram_total_bytes / (1024 * 1024)),
           (unsigned long)(hw.ram_free_bytes / (1024 * 1024)),
           hw.cpu_physical_cores,
           hw.cpu_logical_cores,
           hw.has_gpu ? hw.gpu_name : "none",
           (unsigned long)(hw.vram_total_bytes / (1024 * 1024)),
           (unsigned long)(hw.vram_free_bytes / (1024 * 1024)));

    printf("test_hw: PASS\n");
    return 0;
}
