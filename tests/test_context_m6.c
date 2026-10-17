#include "context.h"
#include "config.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    Config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.model = strdup("test-model");
    cfg.backend = strdup("ollama");

    SkeetCtx *ctx = skeet_ctx_new(&cfg, NULL, NULL, ".");
    assert(ctx != NULL);

    /* 1. Test System Prompt Initialization */
    assert(context_init_system_prompt(ctx, "test-model") == 0);
    assert(ctx->history_count == 1);
    assert(strncmp(ctx->history[0].role.ptr, "system", 6) == 0);
    assert(strstr(ctx->history[0].content.ptr, "Skeet Code") != NULL);

    /* 2. Test Token Estimation */
    size_t tokens_init = context_estimate_tokens(ctx);
    assert(tokens_init > 10);

    /* 3. Add fake conversation with bulky tool results */
    for (int i = 0; i < 6; i++) {
        char user_buf[64];
        snprintf(user_buf, sizeof(user_buf), "User query %d", i);

        ctx->history[ctx->history_count++] = (Message){
            .role = str_from_cstr("user"),
            .content = (Str){ .ptr = strdup(user_buf), .len = strlen(user_buf) },
            .tool_call_id = (Str){ .ptr = NULL, .len = 0 },
            .tool_calls_json = (Str){ .ptr = NULL, .len = 0 }
        };

        char bulky_output[1024];
        memset(bulky_output, 'A', sizeof(bulky_output) - 1);
        bulky_output[sizeof(bulky_output) - 1] = '\0';

        ctx->history[ctx->history_count++] = (Message){
            .role = str_from_cstr("tool"),
            .content = (Str){ .ptr = strdup(bulky_output), .len = strlen(bulky_output) },
            .tool_call_id = (Str){ .ptr = strdup("call_123"), .len = 8 },
            .tool_calls_json = (Str){ .ptr = NULL, .len = 0 }
        };
    }

    size_t tokens_before = context_estimate_tokens(ctx);
    assert(tokens_before > tokens_init);

    /* 4. Test Context Compaction */
    int freed = context_compact(ctx, 0);
    assert(freed > 0);
    size_t tokens_after = context_estimate_tokens(ctx);
    assert(tokens_after < tokens_before);

    /* 5. Test Session Save and Load */
    assert(session_save(ctx, "unit_test_session") == 0);

    /* Verify session list */
    char **names = NULL;
    size_t count = 0;
    assert(session_list(ctx, &names, &count) == 0);
    assert(count > 0);
    int found_session = 0;
    for (size_t i = 0; i < count; i++) {
        if (strcmp(names[i], "unit_test_session") == 0) {
            found_session = 1;
        }
    }
    assert(found_session == 1);
    session_list_free(names, count);

    /* Create a second fresh context and load session */
    SkeetCtx *ctx2 = skeet_ctx_new(&cfg, NULL, NULL, ".");
    assert(ctx2 != NULL);
    assert(session_load(ctx2, "unit_test_session") == 0);
    assert(ctx2->history_count == ctx->history_count);
    assert(strncmp(ctx2->history[0].role.ptr, "system", 6) == 0);

    skeet_ctx_free(ctx2);
    skeet_ctx_free(ctx);
    free(cfg.model);
    free(cfg.backend);

    printf("test_context_m6: PASS\n");
    return 0;
}

