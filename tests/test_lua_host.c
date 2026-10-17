#include "plugins/lua_host.h"
#include "tools/registry.h"
#include "commands.h"
#include "context.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    SkeetCtx *ctx = skeet_ctx_new(NULL, NULL, NULL, ".");
    assert(ctx != NULL);

    LuaHost *host = lua_host_new(ctx);
    assert(host != NULL);

    if (!lua_host_is_available(host)) {
        printf("Lua dynamic runtime not found in environment, graceful fallback tested.\n");
        lua_host_free(host);
        skeet_ctx_free(ctx);
        printf("test_lua_host: PASS (skipped execution)\n");
        return 0;
    }

    /* Test evaluation of skeet.register_tool and register_command */
    const char *script =
        "skeet.log(skeet.LOG_INFO, 'Hello from Lua test chunk!')\n"
        "skeet.register_tool({\n"
        "    name = 'lua_add',\n"
        "    description = 'Adds two numbers',\n"
        "    schema = '{\"type\":\"object\"}',\n"
        "    run = function(args)\n"
        "        return 'lua_add_result'\n"
        "    end\n"
        "})\n"
        "skeet.register_command('/lua_cmd', 'Test command', function(args)\n"
        "    -- do nothing\n"
        "end)\n";

    assert(lua_host_eval(host, script) == 0);

    /* Verify tool registration */
    const ToolDef *t = tool_find(ctx, "lua_add");
    assert(t != NULL);
    assert(strcmp(t->name, "lua_add") == 0);

    /* Verify tool execution */
    ToolCall call = {
        .id = str_from_cstr("1"),
        .name = str_from_cstr("lua_add"),
        .args_json = str_from_cstr("{}")
    };
    ToolResult res;
    memset(&res, 0, sizeof(res));
    assert(t->run(ctx, &call, &res, t->userdata) == 0);
    assert(res.is_error == 0);
    assert(res.content.ptr != NULL);
    assert(strcmp(res.content.ptr, "lua_add_result") == 0);

    /* Clean up */
    lua_host_free(host);
    assert(tool_find(ctx, "lua_add") == NULL);

    skeet_ctx_free(ctx);
    printf("test_lua_host: PASS\n");
    return 0;
}

