#ifndef SKEET_PLUGIN_LOADER_H
#define SKEET_PLUGIN_LOADER_H

#include "context.h"
#include "plugin_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PluginInstance {
    char *path;
    char *owner;
    void *lib_handle;
    int (*deinit_fn)(void);
} PluginInstance;

typedef struct PluginManager {
    SkeetCtx *ctx;
    PluginInstance *plugins;
    size_t count;
    size_t cap;
} PluginManager;

PluginManager *plugin_manager_new(SkeetCtx *ctx);
void plugin_manager_free(PluginManager *pm);

/* Loads a .so / .dll / cdylib plugin from path */
int plugin_load(PluginManager *pm, const char *path);

/* Scans directory for plugins (.so on Linux, .dylib on macOS, .dll on Windows) and loads them */
int plugin_load_directory(PluginManager *pm, const char *dir_path);

/* Unloads all plugins and unregisters their tools/commands */
void plugin_unload_all(PluginManager *pm);

#ifdef __cplusplus
}
#endif

#endif /* SKEET_PLUGIN_LOADER_H */

