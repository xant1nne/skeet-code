#ifndef SKEET_PLUGIN_ABI_H
#define SKEET_PLUGIN_ABI_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

#define SKEET_PLUGIN_ABI_VERSION 1

// Tool flags
#define SKEET_TOOL_READONLY       (1 << 0)
#define SKEET_TOOL_NEEDS_CONFIRM  (1 << 1)

// Log levels
#define SKEET_LOG_DEBUG 0
#define SKEET_LOG_INFO  1
#define SKEET_LOG_WARN  2
#define SKEET_LOG_ERROR 3

// Return codes
#define SKEET_OK   0
#define SKEET_ERR -1

// Callback executed when the agent calls a plugin tool.
// args_json: JSON string of arguments from model
// out_result: allocated string with result (caller or host frees via free_func or host free)
// out_is_error: set to 1 if tool failed, 0 if successful
// Return 0 on success, -1 on failure
typedef int (*skeet_tool_exec_fn)(const char *args_json, char **out_result, int *out_is_error, void *userdata);

// Callback executed when a slash command registered by plugin is typed
typedef int (*skeet_cmd_exec_fn)(const char *args, char **out_msg, void *userdata);

// Host logging callback
typedef void (*skeet_log_fn)(int level, const char *msg);

// Host memory free callback
typedef void (*skeet_free_fn)(void *ptr);

// Registration functions provided by Skeet host
typedef int (*skeet_host_register_tool_fn)(
    const char *name,
    const char *description,
    const char *schema_json,
    uint32_t flags,
    skeet_tool_exec_fn exec,
    void *userdata
);

typedef int (*skeet_host_register_cmd_fn)(
    const char *name,
    const char *help,
    skeet_cmd_exec_fn exec,
    void *userdata
);

// Host API provided to plugins during init
typedef struct SkeetHostApi {
    uint32_t size;
    const char *owner;
    const char *workspace_root;
    skeet_host_register_tool_fn register_tool;
    skeet_host_register_cmd_fn register_command;
    skeet_log_fn log;
    skeet_free_fn free_func;
} SkeetHostApi;

// Exported plugin symbols (entry points implemented by .so / .dll / cdylib)

// Returns SKEET_PLUGIN_ABI_VERSION
uint32_t skeet_plugin_abi_version(void);

// Initialize the plugin. Host passes host_api structure.
// Return 0 on success, -1 on error.
int skeet_plugin_init(const SkeetHostApi *host_api);

// Deinitialize the plugin before unload. Return 0 on success.
int skeet_plugin_deinit(void);

#ifdef __cplusplus
}
#endif

#endif // SKEET_PLUGIN_ABI_H
