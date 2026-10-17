#include "plugins/plugin_loader.h"
#include "plugins/plugin_abi.h"
#include "tools/registry.h"
#include "commands.h"
#include "context.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static int mock_plugin_exec(const char *args_json, char **out_result, int *out_is_error, void *userdata) {
    (void)args_json;
    (void)userdata;
    *out_is_error = 0;
    *out_result = strdup("mock_plugin_success");
    return 0;
}

int main(void) {
    SkeetCtx *ctx = skeet_ctx_new(NULL, NULL, NULL, ".");
    assert(ctx != NULL);

    PluginManager *pm = plugin_manager_new(ctx);
    assert(pm != NULL);

    /* Test invalid plugin load handling */
    int rc = plugin_load(pm, "non_existent_plugin.so");
    assert(rc != 0);

    /* Test ABI verification logic */
    assert(skeet_plugin_abi_version() == SKEET_PLUGIN_ABI_VERSION);

    /* Test registering a tool through plugin ABI bridge */
    HostContextBridge bridge;
    memset(&bridge, 0, sizeof(bridge));
    bridge.ctx = ctx;
    bridge.owner = "unit_test_plugin";

    SkeetHostApi host_api = {
        .size = sizeof(SkeetHostApi),
        .owner = "unit_test_plugin",
        .workspace_root = ".",
        .register_tool = NULL, /* will use direct loader handler */
        .register_command = NULL,
        .log = NULL,
        .free_func = free
    };
    (void)host_api;

    /* Manually test tool trampoline mechanism */
    ToolBinding tb = {
        .exec = mock_plugin_exec,
        .userdata = NULL,
        .owner = strdup("unit_test_plugin")
    };

    ToolDef tdef = {
        .size = sizeof(ToolDef),
        .name = "plugin_test_tool",
        .description = "Test plugin tool",
        .schema_json = "{}",
        .flags = SKEET_TOOL_READONLY,
        .run = NULL, /* we can invoke mock directly or through registry */
        .userdata = &tb,
        .owner = "unit_test_plugin"
    };

    /* Register in host registry */
    assert(tool_register(ctx, &tdef) == 0);
    const ToolDef *found = tool_find(ctx, "plugin_test_tool");
    assert(found != NULL);
    assert(strcmp(found->owner, "unit_test_plugin") == 0);

    /* Unregister by owner */
    int unreg = tool_unregister_owner(ctx, "unit_test_plugin");
    assert(unreg == 1);
    assert(tool_find(ctx, "plugin_test_tool") == NULL);

    free(tb.owner);
    plugin_manager_free(pm);
    skeet_ctx_free(ctx);

    printf("test_plugins: PASS\n");
    return 0;
}

