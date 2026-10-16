#include "permissions.h"
#include "context.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static int mock_confirm(SkeetCtx *ctx, const ToolCall *call, const char *preview, void *userdata) {
    (void)ctx;
    (void)call;
    (void)preview;
    int *action = (int *)userdata;
    return *action;
}

int main(void) {
    SkeetCtx *ctx = skeet_ctx_new(NULL, NULL, NULL, ".");
    assert(ctx != NULL);

    ToolCall ro_call = {
        .id = str_from_cstr("c1"),
        .name = str_from_cstr("read_file"),
        .args_json = str_from_cstr("{}")
    };
    ToolDef ro_def = {
        .size = sizeof(ToolDef),
        .name = "read_file",
        .flags = TOOL_READONLY
    };

    ToolCall mut_call = {
        .id = str_from_cstr("c2"),
        .name = str_from_cstr("write_file"),
        .args_json = str_from_cstr("{}")
    };
    ToolDef mut_def = {
        .size = sizeof(ToolDef),
        .name = "write_file",
        .flags = TOOL_NEEDS_CONFIRM
    };

    /* In YES mode, everything allowed */
    perm_set_mode(ctx, PERM_MODE_YES);
    assert(perm_check(ctx, &ro_call, &ro_def, NULL) == 1);
    assert(perm_check(ctx, &mut_call, &mut_def, NULL) == 1);

    /* In READONLY mode, mutating tools denied */
    perm_set_mode(ctx, PERM_MODE_READONLY);
    assert(perm_check(ctx, &ro_call, &ro_def, NULL) == 1);
    assert(perm_check(ctx, &mut_call, &mut_def, NULL) == 0);

    /* In ASK mode with mock confirm */
    perm_set_mode(ctx, PERM_MODE_ASK);
    int confirm_result = 0;
    perm_set_confirm_fn(ctx, mock_confirm, &confirm_result);

    assert(perm_check(ctx, &ro_call, &ro_def, NULL) == 1);
    assert(perm_check(ctx, &mut_call, &mut_def, NULL) == 0);

    confirm_result = 1;
    assert(perm_check(ctx, &mut_call, &mut_def, NULL) == 1);

    /* Test always allow (2) */
    confirm_result = 2;
    assert(perm_check(ctx, &mut_call, &mut_def, NULL) == 1);
    assert(perm_get_tool_action(ctx, "write_file") == PERM_ALLOW);

    confirm_result = 0;
    assert(perm_check(ctx, &mut_call, &mut_def, NULL) == 1);

    skeet_ctx_free(ctx);
    printf("test_permissions: PASS\n");
    return 0;
}
