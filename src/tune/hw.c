#include "tune/hw.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <sys/sysctl.h>
#include <sys/types.h>
#include <mach/mach.h>
#elif defined(__linux__)
#include <sys/sysinfo.h>
#include <dlfcn.h>
#elif defined(_WIN32)
#include <windows.h>
#endif

static void detect_ram(HwInfo *out) {
#if defined(__linux__)
    struct sysinfo si;
    if (sysinfo(&si) == 0) {
        out->ram_total_bytes = (uint64_t)si.totalram * si.mem_unit;
        out->ram_free_bytes = (uint64_t)si.freeram * si.mem_unit;
    }

    FILE *f = fopen("/proc/meminfo", "r");
    if (f) {
        char line[256];
        uint64_t total_kb = 0;
        uint64_t avail_kb = 0;
        while (fgets(line, sizeof(line), f)) {
            if (strncmp(line, "MemTotal:", 9) == 0) {
                sscanf(line + 9, "%lu", &total_kb);
            } else if (strncmp(line, "MemAvailable:", 13) == 0) {
                sscanf(line + 13, "%lu", &avail_kb);
            }
        }
        fclose(f);
        if (total_kb > 0) {
            out->ram_total_bytes = total_kb * 1024;
        }
        if (avail_kb > 0) {
            out->ram_free_bytes = avail_kb * 1024;
        }
    }
#elif defined(__APPLE__)
    int mib[2] = {CTL_HW, HW_MEMSIZE};
    uint64_t memsize = 0;
    size_t len = sizeof(memsize);
    if (sysctl(mib, 2, &memsize, &len, NULL, 0) == 0) {
        out->ram_total_bytes = memsize;
        out->ram_free_bytes = memsize / 2;
    }
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    vm_statistics64_data_t vm_stat;
    if (host_statistics64(mach_host_self(), HOST_VM_INFO64, (host_info64_t)&vm_stat, &count) == KERN_SUCCESS) {
        uint64_t page_size = sysconf(_SC_PAGESIZE);
        out->ram_free_bytes = (uint64_t)(vm_stat.free_count + vm_stat.inactive_count) * page_size;
    }
#elif defined(_WIN32)
    MEMORYSTATUSEX mem_status;
    mem_status.dwLength = sizeof(mem_status);
    if (GlobalMemoryStatusEx(&mem_status)) {
        out->ram_total_bytes = mem_status.ullTotalPhys;
        out->ram_free_bytes = mem_status.ullAvailPhys;
    }
#endif
}

static void detect_cpu(HwInfo *out) {
    long logical = sysconf(_SC_NPROCESSORS_ONLN);
    out->cpu_logical_cores = logical > 0 ? (int)logical : 1;

    int physical_cores = 0;
#if defined(__linux__)
    int seen_core_ids[256];
    int seen_count = 0;

    for (int i = 0; i < out->cpu_logical_cores && i < 256; i++) {
        char path[256];
        snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/topology/core_id", i);
        FILE *f = fopen(path, "r");
        if (f) {
            int cid = -1;
            if (fscanf(f, "%d", &cid) == 1) {
                int found = 0;
                for (int j = 0; j < seen_count; j++) {
                    if (seen_core_ids[j] == cid) {
                        found = 1;
                        break;
                    }
                }
                if (!found && seen_count < 256) {
                    seen_core_ids[seen_count++] = cid;
                }
            }
            fclose(f);
        }
    }

    if (seen_count > 0) {
        physical_cores = seen_count;
    } else {
        physical_cores = out->cpu_logical_cores > 1 ? out->cpu_logical_cores / 2 : 1;
    }
#elif defined(__APPLE__)
    int mib[2] = {CTL_HW, HW_PHYSICALCPU};
    int cores = 0;
    size_t len = sizeof(cores);
    if (sysctl(mib, 2, &cores, &len, NULL, 0) == 0 && cores > 0) {
        physical_cores = cores;
    } else {
        physical_cores = out->cpu_logical_cores > 1 ? out->cpu_logical_cores / 2 : 1;
    }
#else
    physical_cores = out->cpu_logical_cores > 1 ? out->cpu_logical_cores / 2 : 1;
#endif

    out->cpu_physical_cores = physical_cores > 0 ? physical_cores : 1;
}

#if defined(__linux__)
typedef int (*nvml_fn_init)(void);
typedef int (*nvml_fn_shutdown)(void);
typedef int (*nvml_fn_get_count)(unsigned int *);
typedef int (*nvml_fn_get_handle)(unsigned int, void **);
typedef int (*nvml_fn_get_name)(void *, char *, unsigned int);
typedef struct {
    unsigned long long total;
    unsigned long long free;
    unsigned long long used;
} nvml_mem_info_t;
typedef int (*nvml_fn_get_mem)(void *, nvml_mem_info_t *);

static int detect_gpu_nvml(HwInfo *out) {
    const char *libs[] = {"libnvidia-ml.so.1", "libnvidia-ml.so"};
    void *h = NULL;
    for (size_t i = 0; i < sizeof(libs) / sizeof(libs[0]); i++) {
        h = dlopen(libs[i], RTLD_LAZY);
        if (h) {
            break;
        }
    }
    if (!h) {
        return -1;
    }

    nvml_fn_init fn_init = NULL;
    *(void **)(&fn_init) = dlsym(h, "nvmlInit_v2");
    if (!fn_init) {
        *(void **)(&fn_init) = dlsym(h, "nvmlInit");
    }
    nvml_fn_shutdown fn_shutdown = NULL;
    *(void **)(&fn_shutdown) = dlsym(h, "nvmlShutdown");
    nvml_fn_get_handle fn_get_handle = NULL;
    *(void **)(&fn_get_handle) = dlsym(h, "nvmlDeviceGetHandleByIndex_v2");
    if (!fn_get_handle) {
        *(void **)(&fn_get_handle) = dlsym(h, "nvmlDeviceGetHandleByIndex");
    }
    nvml_fn_get_name fn_get_name = NULL;
    *(void **)(&fn_get_name) = dlsym(h, "nvmlDeviceGetName");
    nvml_fn_get_mem fn_get_mem = NULL;
    *(void **)(&fn_get_mem) = dlsym(h, "nvmlDeviceGetMemoryInfo");

    if (!fn_init || !fn_shutdown || !fn_get_handle || !fn_get_name || !fn_get_mem) {
        dlclose(h);
        return -1;
    }

    if (fn_init() != 0) {
        dlclose(h);
        return -1;
    }

    void *dev = NULL;
    int ok = -1;
    if (fn_get_handle(0, &dev) == 0 && dev) {
        char name[128];
        if (fn_get_name(dev, name, sizeof(name)) == 0) {
            strncpy(out->gpu_name, name, sizeof(out->gpu_name) - 1);
            out->gpu_name[sizeof(out->gpu_name) - 1] = '\0';
        }
        nvml_mem_info_t mem;
        if (fn_get_mem(dev, &mem) == 0) {
            out->vram_total_bytes = mem.total;
            out->vram_free_bytes = mem.free;
            out->has_gpu = 1;
            ok = 0;
        }
    }

    fn_shutdown();
    dlclose(h);
    return ok;
}
#endif

static void detect_gpu_nvidia(HwInfo *out) {
#if defined(__linux__)
    if (detect_gpu_nvml(out) == 0) {
        return;
    }
#endif

    FILE *p = popen("nvidia-smi --query-gpu=name,memory.total,memory.free --format=csv,noheader,nounits 2>/dev/null", "r");
    if (!p) {
        return;
    }

    char line[512];
    if (fgets(line, sizeof(line), p)) {
        char *comma1 = strchr(line, ',');
        if (comma1) {
            *comma1 = '\0';
            strncpy(out->gpu_name, line, sizeof(out->gpu_name) - 1);
            out->gpu_name[sizeof(out->gpu_name) - 1] = '\0';

            char *total_str = comma1 + 1;
            char *comma2 = strchr(total_str, ',');
            if (comma2) {
                *comma2 = '\0';
                char *free_str = comma2 + 1;

                uint64_t total_mb = 0;
                uint64_t free_mb = 0;
                sscanf(total_str, "%lu", &total_mb);
                sscanf(free_str, "%lu", &free_mb);

                if (total_mb > 0) {
                    out->vram_total_bytes = total_mb * 1024 * 1024;
                    out->vram_free_bytes = free_mb * 1024 * 1024;
                    out->has_gpu = 1;
                }
            }
        }
    }

    pclose(p);
}

static void detect_gpu_amd(HwInfo *out) {
    if (out->has_gpu) {
        return;
    }

    FILE *f = fopen("/sys/class/drm/card0/device/mem_info_vram_total", "r");
    if (f) {
        uint64_t total = 0;
        if (fscanf(f, "%lu", &total) == 1 && total > 0) {
            out->vram_total_bytes = total;
            out->vram_free_bytes = total;
            strncpy(out->gpu_name, "AMD GPU", sizeof(out->gpu_name) - 1);
            out->has_gpu = 1;
        }
        fclose(f);
    }
}

static void detect_gpu_apple(HwInfo *out) {
#if defined(__APPLE__)
    if (out->has_gpu) {
        return;
    }
    int mib[2] = {CTL_HW, HW_MEMSIZE};
    uint64_t memsize = 0;
    size_t len = sizeof(memsize);
    if (sysctl(mib, 2, &memsize, &len, NULL, 0) == 0 && memsize > 0) {
        out->vram_total_bytes = (memsize * 3) / 4;
        out->vram_free_bytes = out->ram_free_bytes > 0 ? (out->ram_free_bytes * 3) / 4 : out->vram_total_bytes / 2;
        strncpy(out->gpu_name, "Apple Silicon (Metal)", sizeof(out->gpu_name) - 1);
        out->gpu_name[sizeof(out->gpu_name) - 1] = '\0';
        out->has_gpu = 1;
    }
#else
    (void)out;
#endif
}

int hw_detect(HwInfo *out) {
    if (!out) {
        return -1;
    }
    memset(out, 0, sizeof(*out));

    detect_ram(out);
    detect_cpu(out);
    detect_gpu_apple(out);
    detect_gpu_nvidia(out);
    detect_gpu_amd(out);

    return 0;
}
