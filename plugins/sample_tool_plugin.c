#include "src/plugins/plugin_abi.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
    #define EXPORT __declspec(dllexport)
#else
    #define EXPORT __attribute__((visibility("default")))
#endif

EXPORT uint32_t skeet_plugin_abi_version(void) {
    return SKEET_PLUGIN_ABI_VERSION;
}

static int sample_system_info_exec(const char *args_json, char **out_result, int *out_is_error, void *userdata) {
    (void)args_json;
    (void)userdata;
    *out_is_error = 0;

    const char *info = "Sample Plugin Tool (C/Rust dynamic plugin):\n"
                       "- OS Plugin Architecture: Loaded successfully as dynamic library (.so/.dll)\n"
                       "- Interface: SkeetPluginABI v1\n"
                       "- Status: Operational";
    *out_result = strdup(info);
    return 0;
}

static int sample_hello_cmd(const char *args, char **out_msg, void *userdata) {
    (void)userdata;
    char buf[256];
    snprintf(buf, sizeof(buf), "Hello from dynamic plugin! Args: %s", args ? args : "(none)");
    *out_msg = strdup(buf);
    return 0;
}

EXPORT int skeet_plugin_init(const SkeetHostApi *host_api) {
    if (!host_api) return -1;
    host_api->log(SKEET_LOG_INFO, "Initializing sample dynamic plugin...");

    const char *schema = "{\"type\":\"object\",\"properties\":{},\"additionalProperties\":false}";

    host_api->register_tool(
        "sample_plugin_info",
        "Returns diagnostic information about the loaded dynamic plugin",
        schema,
        SKEET_TOOL_READONLY,
        sample_system_info_exec,
        NULL
    );

    host_api->register_command(
        "/plugin_hello",
        "Test command from dynamic library plugin",
        sample_hello_cmd,
        NULL
    );

    host_api->log(SKEET_LOG_INFO, "Registered 'sample_plugin_info' and '/plugin_hello'");
    return 0;
}

EXPORT int skeet_plugin_deinit(void) {
    return 0;
}

