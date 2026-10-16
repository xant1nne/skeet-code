#include "tools/registry.h"
#include "context.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static int mock_tool_run(SkeetCtx *ctx, const ToolCall *call, ToolResult *result, void *userdata) {
    (void)ctx;
    (void)call;
    (void)userdata;
    result->is_error = 0;
    result->content = str_from_cstr("ok");
    return 0;
}

int main(void) {
    SkeetCtx *ctx = skeet_ctx_new(NULL, NULL, NULL, ".");
    assert(ctx != NULL);

    ToolDef t1 = {
        .size = sizeof(ToolDef),
        .name = "my_tool",
        .description = "My test tool",
        .schema_json = "{\"type\":\"object\"}",
        .flags = TOOL_READONLY,
        .run = mock_tool_run,
        .userdata = NULL,
        .owner = "test_owner"
    };

    assert(tool_register(ctx, &t1) == 0);
    assert(tool_registry_count(ctx) == 1);

    const ToolDef *found = tool_find(ctx, "my_tool");
    assert(found != NULL);
    assert(strcmp(found->name, "my_tool") == 0);
    assert(found->flags == TOOL_READONLY);

    Str json_out;
    assert(tools_build_request_json(ctx, &json_out) == 0);
    assert(json_out.ptr != NULL);
    assert(strstr(json_out.ptr, "my_tool") != NULL);

    int unreg = tool_unregister_owner(ctx, "test_owner");
    assert(unreg == 1);
    assert(tool_find(ctx, "my_tool") == NULL);
    assert(tool_registry_count(ctx) == 0);

    skeet_ctx_free(ctx);
    printf("test_registry: PASS\n");
    return 0;
}
