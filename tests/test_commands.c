#include "commands.h"
#include "context.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static int g_custom_called = 0;

static int cmd_custom(SkeetCtx *ctx, const char *args, void *userdata) {
    (void)ctx;
    (void)userdata;
    if (strcmp(args, "test_arg") == 0) {
        g_custom_called = 1;
    }
    return 42;
}

int main(void) {
    SkeetCtx *ctx = skeet_ctx_new(NULL, NULL, NULL, ".");
    assert(ctx != NULL);

    int handled = 0;
    int rc = command_dispatch(ctx, "regular prompt", &handled);
    assert(handled == 0);
    assert(rc == 0);

    rc = command_dispatch(ctx, "/help", &handled);
    assert(handled == 1);
    assert(rc == 0);

    CommandDef cdef = {
        .size = sizeof(CommandDef),
        .name = "/custom",
        .help = "Custom command for testing",
        .handler = cmd_custom,
        .userdata = NULL,
        .owner = "test_plugin"
    };
    assert(command_register(ctx, &cdef) == 0);

    rc = command_dispatch(ctx, "/custom test_arg", &handled);
    assert(handled == 1);
    assert(rc == 42);
    assert(g_custom_called == 1);

    int unreg = command_unregister_owner(ctx, "test_plugin");
    assert(unreg == 1);

    rc = command_dispatch(ctx, "/custom", &handled);
    assert(handled == 1);
    assert(rc == 0);

    rc = command_dispatch(ctx, "/exit", &handled);
    assert(handled == 1);
    assert(rc == 1);

    skeet_ctx_free(ctx);
    printf("test_commands: PASS\n");
    return 0;
}
