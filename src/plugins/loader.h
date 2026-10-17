#pragma once

#include "plugin_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

// Initialize the plugin loader with host API (must remain valid during loader lifetime)
int plugins_loader_init(const SkeetHostApi *host_api);

// Scan a directory for shared objects and load them (returns number loaded or negative on error)
int plugins_loader_load_dir(const char *dir_path);

// Load plugins from default locations (~/.config/skeet/plugins, .skeet/plugins)
int plugins_loader_load_default_paths(void);

// Unload all loaded plugins
void plugins_loader_unload_all(void);

#ifdef __cplusplus
}
#endif
