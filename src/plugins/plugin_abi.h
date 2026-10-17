#ifndef SKEET_PLUGIN_ABI_H
#define SKEET_PLUGIN_ABI_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

// Log levels (host-defined)
#define SKEET_LOG_DEBUG 0
#define SKEET_LOG_INFO  1
#define SKEET_LOG_WARN  2
#define SKEET_LOG_ERROR 3

// Return codes
#define SKEET_OK 0
#define SKEET_ERR -1

// Host API provided to plugins. All strings are NUL-terminated UTF-8.
// register_tool / register_command expect a JSON string describing the tool/command.
// call_host_tool should allocate a NUL-terminated JSON string in *out_json using
// the host allocator (or return NULL on error). The host is responsible for
// freeing out_json with host->free_func if provided.

typedef int (*skeet_register_fn)(const char *json);
typedef void (*skeet_log_fn)(int level, const char *msg);
typedef int (*skeet_call_tool_fn)(const char *tool_name, const char *args_json, char **out_json);

typedef void (*skeet_free_fn)(void *ptr);

typedef struct SkeetHostApi {
    skeet_register_fn register_tool;    // register a tool by JSON metadata
    skeet_register_fn register_command; // register a slash command by JSON metadata
    skeet_log_fn log;                   // logging callback
    skeet_call_tool_fn call_host_tool;  // call a host-provided tool (optional)
    skeet_free_fn free_func;            // host-provided free for returned pointers (optional)
    const char *owner;                  // owner identifier string (NUL-terminated)
} SkeetHostApi;

// Plugin entry points (exported symbols)

// Returns ABI version supported by plugin
uint32_t skeet_plugin_abi_version(void);

// Initialize the plugin. host_api is owned by the host and valid during init.
// Plugin should call register_tool/register_command to register capabilities.
// Return 0 on success, non-zero on error.
int skeet_plugin_init(const SkeetHostApi *host_api);

// Optional deinit called before unload. Return 0 on success.
int skeet_plugin_deinit(void);

#ifdef __cplusplus
}
#endif

#endif // SKEET_PLUGIN_ABI_H
