#include "agent.h"
#include "commands.h"
#include "events.h"
#include "log.h"
#include "permissions.h"
#include "tools/accumulator.h"
#include "tools/builtins.h"
#include "tools/diff.h"
#include "tools/registry.h"
#include "tools/text_fallback.h"
#include "tui/tui.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_TOOL_ITERATIONS 25

static volatile sig_atomic_t *g_cancel_ptr = NULL;
static volatile sig_atomic_t g_in_stream = 0;
static volatile sig_atomic_t g_sigint_received = 0;

static void sigint_handler(int sig) {
    (void)sig;
    g_sigint_received = 1;
    if (g_in_stream && g_cancel_ptr) {
        *g_cancel_ptr = 1;
    }
}

typedef struct AgentStreamCtx {
    SkeetCtx *ctx;
    ToolCallAccumulator acc;
    StrBuf text_buf;
    int first_token;
    Tui *tui;
    size_t token_count;
    clock_t start_time;
} AgentStreamCtx;

static void agent_stream_callback(const char *data, size_t len, void *userdata) {
    AgentStreamCtx *sctx = (AgentStreamCtx *)userdata;
    if (!data || len == 0) {
        return;
    }

    char *text_chunk = NULL;
    char *err_msg = NULL;
    int rc = accumulator_feed_sse_json(&sctx->acc, data, len, &text_chunk, &err_msg);
    if (rc == 0) {
        if (text_chunk && text_chunk[0]) {
            if (sctx->first_token) {
                sctx->first_token = 0;
            }
            sctx->token_count++;

            if (sctx->tui && sctx->tui->is_interactive) {
                tui_stream_token(sctx->tui, text_chunk);
            } else {
                fwrite(text_chunk, 1, strlen(text_chunk), stdout);
                fflush(stdout);
            }

            EventData ev;
            memset(&ev, 0, sizeof(ev));
            ev.stream_token.token = text_chunk;
            ev.stream_token.token_len = strlen(text_chunk);
            event_emit(sctx->ctx, EVENT_STREAM_TOKEN, &ev);

            strbuf_puts(&sctx->text_buf, text_chunk);
            free(text_chunk);
        }
        if (err_msg) {
            if (sctx->tui && sctx->tui->is_interactive) {
                tui_append_error(sctx->tui, err_msg);
            } else {
                printf("\n\033[1;31m[error]\033[0m %s\n", err_msg);
            }
            free(err_msg);
        }
    }
}

static void ensure_history_cap(SkeetCtx *ctx, size_t needed) {
    if (ctx->history_count + needed > ctx->history_cap) {
        size_t new_cap = ctx->history_cap ? ctx->history_cap * 2 : 64;
        while (new_cap < ctx->history_count + needed) {
            new_cap *= 2;
        }
        Message *new_h = realloc(ctx->history, new_cap * sizeof(Message));
        if (new_h) {
            ctx->history = new_h;
            ctx->history_cap = new_cap;
        }
    }
}

int agent_repl(SkeetCtx *ctx, const char *model) {
    if (!ctx || !ctx->backend || !model) {
        return -1;
    }

    g_cancel_ptr = ctx->cancel;
    ctx->backend->cancel = ctx->cancel;

#if !defined(SKEET_OS_WINDOWS)
    struct sigaction sa, old_sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigint_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, &old_sa);
#else
    signal(SIGINT, sigint_handler);
#endif

    EventData ev_start;
    memset(&ev_start, 0, sizeof(ev_start));
    event_emit(ctx, EVENT_SESSION_START, &ev_start);

    char *hist_path = config_history_path();
    Tui *tui = tui_new(hist_path);
    free(hist_path);

    context_init_system_prompt(ctx, model);
    size_t current_tokens = context_estimate_tokens(ctx);

    if (tui && tui->is_interactive) {
        tui_start(tui);
        tui_set_status(tui, model, ctx->backend->name, current_tokens, 8192, 0.0, "ready");
    } else {
        printf("\n\033[1;36m╭─ SKEET Code ──────────────────────────────────────────╮\033[0m\n");
        printf("\033[1;36m│\033[0m Model:   %-46s \033[1;36m│\033[0m\n", model);
        printf("\033[1;36m│\033[0m Backend: %-46s \033[1;36m│\033[0m\n", ctx->backend->name);
        printf("\033[1;36m│\033[0m Type /help for commands, /quit or Ctrl+C to exit.      \033[1;36m│\033[0m\n");
        printf("\033[1;36m╰───────────────────────────────────────────────────────╯\033[0m\n\n");
    }

    char input_line[8192];

    while (1) {
        g_in_stream = 0;
        if (ctx->cancel) {
            *ctx->cancel = 0;
        }
        g_sigint_received = 0;

        current_tokens = context_estimate_tokens(ctx);
        if (current_tokens > 6500) {
            int freed = context_compact(ctx, 4500);
            if (freed > 0) {
                current_tokens = context_estimate_tokens(ctx);
            }
        }
        if (tui && tui->is_interactive) {
            tui_set_status(tui, ctx->config && ctx->config->model ? ctx->config->model : model,
                           ctx->backend ? ctx->backend->name : "none",
                           current_tokens, 8192, 0.0, "ready");
        }
        g_sigint_received = 0;

        skeet_pump(ctx);

        int read_rc = 0;
        if (tui && tui->is_interactive) {
            read_rc = tui_read_line(tui, "\033[1;32mskeet>\033[0m ", input_line, sizeof(input_line));
        } else {
            printf("\033[1;32mskeet>\033[0m ");
            fflush(stdout);
            if (!fgets(input_line, sizeof(input_line), stdin)) {
                read_rc = -1;
            } else {
                size_t in_len = strlen(input_line);
                while (in_len > 0 && (input_line[in_len - 1] == '\n' || input_line[in_len - 1] == '\r')) {
                    input_line[--in_len] = '\0';
                }
            }
        }

        if (read_rc != 0 || g_sigint_received) {
            if (!tui || !tui->is_interactive) printf("\nBye.\n");
            break;
        }

        size_t in_len = strlen(input_line);
        if (in_len == 0) {
            continue;
        }

        if (input_line[0] == '/') {
            int handled = 0;
            int exit_code = command_dispatch(ctx, input_line, &handled);
            if (handled && exit_code != 0) {
                if (!tui || !tui->is_interactive) printf("Bye.\n");
                break;
            }
            if (handled) {
                continue;
            }
        }

        if (tui && tui->is_interactive) {
            tui_append_user_message(tui, input_line);
        }

        ensure_history_cap(ctx, 2);
        char *user_copy = strdup(input_line);
        ctx->history[ctx->history_count++] = (Message){
            .role = str_from_cstr("user"),
            .content = (Str){ .ptr = user_copy, .len = in_len },
            .tool_call_id = (Str){ .ptr = NULL, .len = 0 },
            .tool_calls_json = (Str){ .ptr = NULL, .len = 0 }
        };

        EventData ev_user;
        memset(&ev_user, 0, sizeof(ev_user));
        ev_user.user_message.text = (Str){ .ptr = user_copy, .len = in_len };
        event_emit(ctx, EVENT_USER_MESSAGE, &ev_user);

        for (int iter = 0; iter < MAX_TOOL_ITERATIONS; iter++) {
            EventData ev_prompt;
            memset(&ev_prompt, 0, sizeof(ev_prompt));
            event_emit(ctx, EVENT_PROMPT_BUILD, &ev_prompt);

            Request req;
            memset(&req, 0, sizeof(req));
            req.model = str_from_cstr(model);
            req.messages = ctx->history;
            req.message_count = ctx->history_count;

            tools_build_request_json(ctx, &req.tools_json);

            AgentStreamCtx sctx;
            memset(&sctx, 0, sizeof(sctx));
            sctx.ctx = ctx;
            sctx.tui = tui;
            accumulator_init(&sctx.acc);
            strbuf_init(&sctx.text_buf);
            sctx.first_token = 1;
            sctx.start_time = clock();

            if (tui && tui->is_interactive) {
                tui_start_assistant_message(tui);
            }

            g_in_stream = 1;
            if (ctx->cancel) {
                *ctx->cancel = 0;
            }
            g_sigint_received = 0;

            int rc = ctx->backend->chat_stream(ctx->backend, &req, agent_stream_callback, &sctx);
            g_in_stream = 0;

            clock_t end_time = clock();
            double elapsed_sec = (double)(end_time - sctx.start_time) / CLOCKS_PER_SEC;
            double tps = (elapsed_sec > 0.05 && sctx.token_count > 0) ? (double)sctx.token_count / elapsed_sec : 0.0;

            if (tui && tui->is_interactive) {
                tui_set_status(tui, model, ctx->backend->name, sctx.token_count, 0, tps, "idle");
            }

            if (rc == 1 || (ctx->cancel && *ctx->cancel) || g_sigint_received) {
                if (tui && tui->is_interactive) {
                    tui_append_info(tui, "[Interrupted]");
                } else {
                    printf("\n\033[1;33m[Interrupted]\033[0m\n");
                }
                accumulator_free(&sctx.acc);
                strbuf_free(&sctx.text_buf);
                break;
            } else if (rc != 0) {
                if (tui && tui->is_interactive) {
                    tui_append_error(tui, "[Error communicating with model]");
                } else {
                    printf("\n\033[1;31m[Error communicating with model]\033[0m\n");
                }
                accumulator_free(&sctx.acc);
                strbuf_free(&sctx.text_buf);
                break;
            }

            if (!tui || !tui->is_interactive) {
                if (sctx.text_buf.len > 0) {
                    printf("\n");
                }
            }

            ToolCall *calls = NULL;
            size_t call_count = accumulator_get_calls(&sctx.acc, &calls, &ctx->turn_arena);

            if (call_count == 0 && sctx.text_buf.len > 0) {
                call_count = text_parse_fallback_tool_calls(sctx.text_buf.data, sctx.text_buf.len, &calls, &ctx->turn_arena);
            }

            if (call_count == 0) {
                char *asst_text = strbuf_detach(&sctx.text_buf);
                if (asst_text && asst_text[0]) {
                    ensure_history_cap(ctx, 1);
                    ctx->history[ctx->history_count++] = (Message){
                        .role = str_from_cstr("assistant"),
                        .content = (Str){ .ptr = asst_text, .len = strlen(asst_text) },
                        .tool_call_id = (Str){ .ptr = NULL, .len = 0 },
                        .tool_calls_json = (Str){ .ptr = NULL, .len = 0 }
                    };

                    EventData ev_asst;
                    memset(&ev_asst, 0, sizeof(ev_asst));
                    ev_asst.assistant_message.text = (Str){ .ptr = asst_text, .len = strlen(asst_text) };
                    event_emit(ctx, EVENT_ASSISTANT_MESSAGE, &ev_asst);
                } else {
                    free(asst_text);
                }

                if (tui && tui->is_interactive) {
                    tui_end_assistant_message(tui);
                }

                accumulator_free(&sctx.acc);
                EventData ev_end;
                memset(&ev_end, 0, sizeof(ev_end));
                event_emit(ctx, EVENT_TURN_END, &ev_end);
                break;
            }

            char *calls_json = accumulator_to_json(&sctx.acc);
            char *asst_text = strbuf_detach(&sctx.text_buf);

            ensure_history_cap(ctx, 1 + call_count);
            ctx->history[ctx->history_count++] = (Message){
                .role = str_from_cstr("assistant"),
                .content = (Str){ .ptr = asst_text, .len = asst_text ? strlen(asst_text) : 0 },
                .tool_call_id = (Str){ .ptr = NULL, .len = 0 },
                .tool_calls_json = (Str){ .ptr = calls_json, .len = calls_json ? strlen(calls_json) : 0 }
            };

            accumulator_free(&sctx.acc);
            if (tui && tui->is_interactive) {
                tui_end_assistant_message(tui);
            }

            for (size_t c = 0; c < call_count; c++) {
                const ToolCall *tc = &calls[c];
                char tool_name[128];
                size_t nlen = tc->name.len < sizeof(tool_name) - 1 ? tc->name.len : sizeof(tool_name) - 1;
                memcpy(tool_name, tc->name.ptr, nlen);
                tool_name[nlen] = '\0';

                const ToolDef *def = tool_find(ctx, tool_name);

                EventData ev_before;
                memset(&ev_before, 0, sizeof(ev_before));
                ev_before.before_tool.call = tc;
                EventResult ev_res = event_emit(ctx, EVENT_BEFORE_TOOL, &ev_before);

                ToolResult result;
                memset(&result, 0, sizeof(result));

                if (ev_res == EV_DENY) {
                    result.is_error = 1;
                    result.content = str_from_cstr(ev_before.before_tool.deny_reason ? ev_before.before_tool.deny_reason : "Tool execution denied by event policy");
                } else if (!def) {
                    result.is_error = 1;
                    char not_found[256];
                    snprintf(not_found, sizeof(not_found), "Error: Tool '%s' not registered", tool_name);
                    result.content = (Str){ .ptr = arena_strndup(&ctx->turn_arena, not_found, strlen(not_found)), .len = strlen(not_found) };
                } else {
                    int allowed = perm_check(ctx, tc, def, NULL);
                    if (!allowed) {
                        result.is_error = 1;
                        result.content = str_from_cstr("Tool execution denied by permission policy");
                    } else {
                        if (!tui || !tui->is_interactive) {
                            printf("\033[1;34m[tool]\033[0m %s...\n", tool_name);
                        }
                        def->run(ctx, tc, &result, def->userdata);
                    }
                }

                if (tui && tui->is_interactive) {
                    char *args_str = arena_strndup(&ctx->turn_arena, tc->args_json.ptr, tc->args_json.len);
                    tui_append_tool(tui, tool_name, args_str, result.content.ptr, result.is_error);
                }

                EventData ev_after;
                memset(&ev_after, 0, sizeof(ev_after));
                ev_after.after_tool.call = tc;
                ev_after.after_tool.result = &result;
                event_emit(ctx, EVENT_AFTER_TOOL, &ev_after);

                char *res_copy = strdup(result.content.ptr ? result.content.ptr : "");
                char *call_id_copy = strdup(tc->id.ptr ? tc->id.ptr : "");

                ctx->history[ctx->history_count++] = (Message){
                    .role = str_from_cstr("tool"),
                    .content = (Str){ .ptr = res_copy, .len = strlen(res_copy) },
                    .tool_call_id = (Str){ .ptr = call_id_copy, .len = strlen(call_id_copy) },
                    .tool_calls_json = (Str){ .ptr = NULL, .len = 0 }
                };
            }
        }

        arena_reset(&ctx->turn_arena);
    }

    EventData ev_close;
    memset(&ev_close, 0, sizeof(ev_close));
    event_emit(ctx, EVENT_SESSION_END, &ev_close);

    if (tui) {
        tui_free(tui);
    }

#if !defined(SKEET_OS_WINDOWS)
    sigaction(SIGINT, &old_sa, NULL);
#endif
    return 0;
}
