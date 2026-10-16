#include "tools/builtins.h"
#include "tools/registry.h"
#include "context.h"
#include "cJSON.h"
#include "json.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define BASH_DEFAULT_TIMEOUT_MS 120000
#define BASH_MAX_TIMEOUT_MS     600000
#define BASH_MAX_OUTPUT_BYTES   (30 * 1024)

static uint64_t get_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000 + (uint64_t)tv.tv_usec / 1000;
}

static void kill_process_group(pid_t pid) {
    if (pid <= 0) {
        return;
    }
    kill(-pid, SIGTERM);
    usleep(100000);
    kill(-pid, SIGKILL);
}

static int run_bash(SkeetCtx *ctx, const ToolCall *call, ToolResult *result, void *userdata) {
    (void)userdata;
    result->is_error = 1;

    char *args_str = arena_strndup(&ctx->turn_arena, call->args_json.ptr, call->args_json.len);
    if (!args_str) {
        result->content = str_from_cstr("Error: Out of memory");
        return 0;
    }

    cJSON *root = cJSON_Parse(args_str);
    if (!root) {
        result->content = str_from_cstr("Error: Invalid JSON arguments");
        return 0;
    }

    const char *command = json_obj_str(root, "command");
    if (!command) {
        cJSON_Delete(root);
        result->content = str_from_cstr("Error: Missing required argument 'command'");
        return 0;
    }

    int timeout_ms = BASH_DEFAULT_TIMEOUT_MS;
    const cJSON *to_item = json_obj(root, "timeout_ms");
    if (to_item && cJSON_IsNumber(to_item) && to_item->valueint > 0) {
        timeout_ms = to_item->valueint;
        if (timeout_ms > BASH_MAX_TIMEOUT_MS) {
            timeout_ms = BASH_MAX_TIMEOUT_MS;
        }
    }

    int pipefd[2];
    if (pipe(pipefd) != 0) {
        cJSON_Delete(root);
        result->content = str_from_cstr("Error: Failed to create pipe");
        return 0;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        cJSON_Delete(root);
        result->content = str_from_cstr("Error: fork failed");
        return 0;
    }

    if (pid == 0) {
        setpgid(0, 0);
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);

        if (ctx->workspace_root) {
            if (chdir(ctx->workspace_root) != 0) {
                /* continue */
            }
        }

        execl("/bin/sh", "sh", "-c", command, (char *)NULL);
        _exit(127);
    }

    close(pipefd[1]);

    int flags = fcntl(pipefd[0], F_GETFL, 0);
    fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK);

    StrBuf out;
    strbuf_init(&out);

    uint64_t start_time = get_time_ms();
    int timed_out = 0;
    int cancelled = 0;
    int hit_limit = 0;
    char read_buf[2048];

    struct pollfd pfd;
    pfd.fd = pipefd[0];
    pfd.events = POLLIN | POLLHUP | POLLERR;

    while (1) {
        if (ctx->cancel && *ctx->cancel) {
            cancelled = 1;
            kill_process_group(pid);
            break;
        }

        uint64_t now = get_time_ms();
        if (now - start_time >= (uint64_t)timeout_ms) {
            timed_out = 1;
            kill_process_group(pid);
            break;
        }

        int remaining = timeout_ms - (int)(now - start_time);
        if (remaining <= 0) {
            remaining = 10;
        }
        if (remaining > 100) {
            remaining = 100;
        }

        int pret = poll(&pfd, 1, remaining);
        if (pret > 0) {
            while (1) {
                ssize_t n = read(pipefd[0], read_buf, sizeof(read_buf));
                if (n > 0) {
                    if (out.len + (size_t)n <= BASH_MAX_OUTPUT_BYTES) {
                        strbuf_append(&out, read_buf, n);
                    } else if (!hit_limit) {
                        size_t allowed = BASH_MAX_OUTPUT_BYTES > out.len ? BASH_MAX_OUTPUT_BYTES - out.len : 0;
                        if (allowed > 0) {
                            strbuf_append(&out, read_buf, allowed);
                        }
                        strbuf_puts(&out, "\n[Output truncated: reached 30KB limit]\n");
                        hit_limit = 1;
                    }
                } else {
                    break;
                }
            }

            if (pfd.revents & (POLLHUP | POLLERR)) {
                break;
            }
        } else if (pret == 0) {
            int status;
            pid_t wp = waitpid(pid, &status, WNOHANG);
            if (wp == pid) {
                while (1) {
                    ssize_t n = read(pipefd[0], read_buf, sizeof(read_buf));
                    if (n > 0 && !hit_limit) {
                        strbuf_append(&out, read_buf, n);
                    } else {
                        break;
                    }
                }
                break;
            }
        }
    }

    close(pipefd[0]);

    int status = 0;
    waitpid(pid, &status, 0);

    cJSON_Delete(root);

    if (cancelled) {
        strbuf_free(&out);
        result->content = str_from_cstr("Command cancelled by user");
        result->is_error = 1;
        return 0;
    }

    if (timed_out) {
        strbuf_puts(&out, "\n[Command timed out]");
        result->is_error = 1;
    } else if (WIFEXITED(status)) {
        int exit_code = WEXITSTATUS(status);
        if (exit_code != 0) {
            result->is_error = 1;
            char code_str[64];
            snprintf(code_str, sizeof(code_str), "\n[Process exited with code %d]", exit_code);
            strbuf_puts(&out, code_str);
        } else {
            result->is_error = 0;
        }
    } else if (WIFSIGNALED(status)) {
        result->is_error = 1;
        char sig_str[64];
        snprintf(sig_str, sizeof(sig_str), "\n[Process killed by signal %d]", WTERMSIG(status));
        strbuf_puts(&out, sig_str);
    }

    char *detached = strbuf_detach(&out);
    size_t dlen = detached ? strlen(detached) : 0;
    char *arena_res = arena_strndup(&ctx->turn_arena, detached ? detached : "", dlen);
    free(detached);
    result->content = (Str){ .ptr = arena_res, .len = dlen };
    return 0;
}

int tool_register_bash(SkeetCtx *ctx) {
    ToolDef def = {
        .size = sizeof(ToolDef),
        .name = "bash",
        .description = "Execute a shell command with timeout, output truncation, and Ctrl+C handling",
        .schema_json = "{\"type\":\"object\",\"properties\":{\"command\":{\"type\":\"string\",\"description\":\"The shell command to execute\"},\"timeout_ms\":{\"type\":\"integer\",\"description\":\"Optional timeout in ms (default 120000, max 600000)\"}},\"required\":[\"command\"]}",
        .flags = TOOL_NEEDS_CONFIRM,
        .run = run_bash,
        .userdata = NULL,
        .owner = "core"
    };
    return tool_register(ctx, &def);
}
