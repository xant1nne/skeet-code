#include "backend.h"
#include "cJSON.h"
#include "compat.h"
#include "http.h"
#include "json.h"
#include "log.h"

#include "tune/tune.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

typedef struct LlamaBackend {
    char *server_path;
    char *model_path;
    char *host;
    char *custom_flags;
    int port;
    pid_t pid;
    int spawned;
} LlamaBackend;

typedef struct StreamState {
    StreamCb cb;
    void *userdata;
    SseParser parser;
} StreamState;

static char *find_llama_server(const char *configured) {
    if (configured && configured[0] && access(configured, X_OK) == 0) {
        return strdup(configured);
    }

    if (access("./llama-server", X_OK) == 0) {
        return strdup("./llama-server");
    }

    const char *path_env = getenv("PATH");
    if (path_env) {
        char *path_copy = strdup(path_env);
        if (path_copy) {
            char *saveptr = NULL;
            char *dir = strtok_r(path_copy, ":", &saveptr);
            while (dir) {
                char candidate[1024];
                snprintf(candidate, sizeof(candidate), "%s/llama-server", dir);
                if (access(candidate, X_OK) == 0) {
                    free(path_copy);
                    return strdup(candidate);
                }
                dir = strtok_r(NULL, ":", &saveptr);
            }
            free(path_copy);
        }
    }

    const char *common[] = {
        "/usr/local/bin/llama-server",
        "/usr/bin/llama-server",
        "/opt/homebrew/bin/llama-server"
    };
    for (size_t i = 0; i < sizeof(common) / sizeof(common[0]); i++) {
        if (access(common[i], X_OK) == 0) {
            return strdup(common[i]);
        }
    }

    return NULL;
}

static int llama_check_health(const char *host, HttpResponse *resp, volatile sig_atomic_t *cancel) {
    char url[1024];
    snprintf(url, sizeof(url), "%s/health", host);
    return http_get(url, resp, cancel);
}

static int spawn_llama_server(LlamaBackend *lb, const char *model) {
    char *bin = find_llama_server(lb->server_path);
    if (!bin) {
        log_error("llama-server binary not found");
        return -1;
    }

    const char *mpath = (lb->model_path && lb->model_path[0]) ? lb->model_path : model;
    if (!mpath || !mpath[0]) {
        log_error("No GGUF model path specified for llama-server");
        free(bin);
        return -1;
    }

    char port_str[32];
    snprintf(port_str, sizeof(port_str), "%d", lb->port > 0 ? lb->port : 8080);

    TuneParams tp;
    memset(&tp, 0, sizeof(tp));
    if (tune_model(mpath, lb->custom_flags, &tp, NULL, NULL) == 0) {
        log_info("Hardware tuning applied: %s", tp.flags_str);
    }

    char *tokens[64];
    int token_count = 0;
    char flags_buf[512];
    strncpy(flags_buf, tp.flags_str, sizeof(flags_buf) - 1);
    flags_buf[sizeof(flags_buf) - 1] = '\0';

    char *saveptr = NULL;
    char *tok = strtok_r(flags_buf, " \t\r\n", &saveptr);
    while (tok && token_count < 50) {
        tokens[token_count++] = tok;
        tok = strtok_r(NULL, " \t\r\n", &saveptr);
    }

    pid_t pid = fork();
    if (pid < 0) {
        log_error("Failed to fork llama-server: %s", strerror(errno));
        free(bin);
        return -1;
    }

    if (pid == 0) {
        setpgid(0, 0);

        char *args[70];
        int argc = 0;
        args[argc++] = bin;
        args[argc++] = "-m";
        args[argc++] = (char *)mpath;
        args[argc++] = "--port";
        args[argc++] = port_str;
        args[argc++] = "--host";
        args[argc++] = "127.0.0.1";

        for (int i = 0; i < token_count; i++) {
            args[argc++] = tokens[i];
        }
        args[argc] = NULL;

        execv(bin, args);
        _exit(127);
    }

    free(bin);
    lb->pid = pid;
    lb->spawned = 1;
    log_info("Spawned llama-server (pid=%d, port=%s, model=%s)", pid, port_str, mpath);
    return 0;
}

static int llama_start(Backend *b, const char *model) {
    if (!b || !b->userdata) {
        return -1;
    }
    LlamaBackend *lb = (LlamaBackend *)b->userdata;

    HttpResponse resp;
    if (llama_check_health(lb->host, &resp, b->cancel) == 0 && resp.status == 200) {
        http_response_free(&resp);
        log_info("llama-server already running on %s", lb->host);
        return 0;
    }
    http_response_free(&resp);

    if (spawn_llama_server(lb, model) != 0) {
        return -1;
    }

    printf("\033[1;36m[llama.cpp]\033[0m Starting llama-server and loading model...\n");
    fflush(stdout);

    int timeout_sec = 60;
    for (int sec = 0; sec < timeout_sec; sec++) {
        if (b->cancel && *b->cancel) {
            printf("\nCancelled.\n");
            return 1;
        }

        if (lb->pid > 0) {
            int status = 0;
            pid_t wp = waitpid(lb->pid, &status, WNOHANG);
            if (wp == lb->pid) {
                printf("\n\033[1;31m[llama.cpp]\033[0m llama-server terminated unexpectedly (status=%d)\n", status);
                lb->pid = 0;
                lb->spawned = 0;
                return -1;
            }
        }

        int rc = llama_check_health(lb->host, &resp, b->cancel);
        if (rc == 0 && resp.status == 200) {
            http_response_free(&resp);
            printf("\r\033[K\033[1;32m[llama.cpp]\033[0m Model loaded successfully into memory/VRAM.\n");
            fflush(stdout);
            return 0;
        }

        if (rc == 0 && resp.body) {
            cJSON *hjson = cJSON_ParseWithLength(resp.body, resp.body_len);
            if (hjson) {
                const char *st = json_obj_str(hjson, "status");
                printf("\r\033[K\033[1;36m[llama.cpp]\033[0m Status: %s (%ds)...", st ? st : "loading", sec + 1);
                fflush(stdout);
                cJSON_Delete(hjson);
            }
        } else {
            printf("\r\033[K\033[1;36m[llama.cpp]\033[0m Waiting for server healthcheck (%ds)...", sec + 1);
            fflush(stdout);
        }
        http_response_free(&resp);

        usleep(500000);
    }

    printf("\n\033[1;31m[llama.cpp]\033[0m Timed out waiting for llama-server.\n");
    return -1;
}

static int llama_list_models(Backend *b, ModelList *out) {
    if (!b || !b->userdata || !out) {
        return -1;
    }
    LlamaBackend *lb = (LlamaBackend *)b->userdata;
    out->items = NULL;
    out->count = 0;

    char url[1024];
    snprintf(url, sizeof(url), "%s/v1/models", lb->host);

    HttpResponse resp;
    int rc = http_get(url, &resp, b->cancel);
    if (rc == 0 && resp.status == 200 && resp.body) {
        cJSON *root = cJSON_ParseWithLength(resp.body, resp.body_len);
        http_response_free(&resp);
        if (root) {
            const cJSON *data = json_obj_arr(root, "data");
            int len = json_arr_len(data);
            if (len > 0) {
                out->items = calloc(len, sizeof(ModelInfo));
                if (out->items) {
                    for (int i = 0; i < len; i++) {
                        const cJSON *item = json_arr_at(data, i);
                        const char *id = json_obj_str(item, "id");
                        out->items[out->count++].name = strdup(id ? id : "unknown");
                    }
                }
            }
            cJSON_Delete(root);
            return 0;
        }
    } else {
        http_response_free(&resp);
    }

    if (lb->model_path && lb->model_path[0]) {
        out->items = calloc(1, sizeof(ModelInfo));
        if (out->items) {
            const char *base = strrchr(lb->model_path, '/');
            out->items[0].name = strdup(base ? base + 1 : lb->model_path);
            out->count = 1;
            return 0;
        }
    }

    return -1;
}

static void on_sse_event(const char *data, size_t len, void *userdata) {
    StreamState *st = (StreamState *)userdata;
    char *delta = NULL;
    char *err_msg = NULL;

    int rc = json_chat_delta(data, len, &delta, &err_msg);
    if (rc == 0) {
        if (delta && delta[0]) {
            st->cb(delta, strlen(delta), st->userdata);
        }
        if (err_msg) {
            log_error("llama-server stream error: %s", err_msg);
        }
    }
    free(delta);
    free(err_msg);
}

static int on_http_data(const char *data, size_t len, void *userdata) {
    StreamState *st = (StreamState *)userdata;
    return sse_feed(&st->parser, data, len);
}

static int llama_chat_stream(Backend *b, const Request *request, StreamCb cb, void *userdata) {
    if (!b || !b->userdata || !request || !cb) {
        return -1;
    }
    LlamaBackend *lb = (LlamaBackend *)b->userdata;

    char url[1024];
    snprintf(url, sizeof(url), "%s/v1/chat/completions", lb->host);

    char *body = json_chat_body(request);
    if (!body) {
        log_error("Failed to serialize chat request to JSON");
        return -1;
    }

    StreamState state;
    state.cb = cb;
    state.userdata = userdata;
    sse_init(&state.parser, on_sse_event, &state);

    HttpResponse resp;
    int rc = http_post_stream(url, body, on_http_data, &state, &resp, b->cancel);
    free(body);

    if (rc == 0) {
        sse_finish(&state.parser);
    }
    sse_free(&state.parser);

    if (rc != 0) {
        log_warn("llama-server chat stream finished with rc=%d, status=%ld, err=%s",
                 rc, resp.status, resp.error);
    }
    http_response_free(&resp);

    return rc;
}

static void llama_stop(Backend *b) {
    if (!b || !b->userdata) {
        return;
    }
    LlamaBackend *lb = (LlamaBackend *)b->userdata;

    if (lb->spawned && lb->pid > 0) {
        log_info("Stopping llama-server process group %d", lb->pid);
        kill(-lb->pid, SIGINT);

        int status;
        int stopped = 0;
        for (int i = 0; i < 10; i++) {
            pid_t wp = waitpid(lb->pid, &status, WNOHANG);
            if (wp == lb->pid) {
                stopped = 1;
                break;
            }
            usleep(100000);
        }

        if (!stopped) {
            kill(-lb->pid, SIGTERM);
            for (int i = 0; i < 10; i++) {
                pid_t wp = waitpid(lb->pid, &status, WNOHANG);
                if (wp == lb->pid) {
                    stopped = 1;
                    break;
                }
                usleep(100000);
            }
        }

        if (!stopped) {
            kill(-lb->pid, SIGKILL);
            waitpid(lb->pid, &status, 0);
        }

        lb->pid = 0;
        lb->spawned = 0;
    }

    free(lb->server_path);
    free(lb->model_path);
    free(lb->host);
    free(lb->custom_flags);
    free(lb);
    b->userdata = NULL;
}

Backend *backend_llama_new(const char *server_path, const char *model_path, const char *host, int port, const char *custom_flags) {
    Backend *b = calloc(1, sizeof(Backend));
    if (!b) {
        return NULL;
    }

    LlamaBackend *lb = calloc(1, sizeof(LlamaBackend));
    if (!lb) {
        free(b);
        return NULL;
    }

    lb->server_path = server_path ? strdup(server_path) : NULL;
    lb->model_path = model_path ? strdup(model_path) : NULL;
    lb->custom_flags = custom_flags ? strdup(custom_flags) : NULL;
    lb->port = port > 0 ? port : 8080;

    char default_host[128];
    snprintf(default_host, sizeof(default_host), "http://127.0.0.1:%d", lb->port);

    const char *h = (host && host[0]) ? host : default_host;
    size_t hlen = strlen(h);
    while (hlen > 0 && h[hlen - 1] == '/') {
        hlen--;
    }
    lb->host = malloc(hlen + 1);
    if (!lb->host) {
        free(lb->server_path);
        free(lb->model_path);
        free(lb);
        free(b);
        return NULL;
    }
    memcpy(lb->host, h, hlen);
    lb->host[hlen] = '\0';

    b->name = "llama";
    b->userdata = lb;
    b->list_models = llama_list_models;
    b->start = llama_start;
    b->chat_stream = llama_chat_stream;
    b->stop = llama_stop;

    return b;
}
