#include "backend.h"
#include "http.h"
#include "json.h"
#include "log.h"
#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct OllamaBackend {
    char *host;
} OllamaBackend;

void model_list_free(ModelList *list) {
    if (!list) {
        return;
    }
    for (size_t i = 0; i < list->count; i++) {
        free(list->items[i].name);
    }
    free(list->items);
    list->items = NULL;
    list->count = 0;
}

void backend_free(Backend *backend) {
    if (!backend) {
        return;
    }
    if (backend->stop) {
        backend->stop(backend);
    }
    free(backend);
}

static int ollama_list_models(Backend *b, ModelList *out) {
    if (!b || !b->userdata || !out) {
        return -1;
    }
    out->items = NULL;
    out->count = 0;

    OllamaBackend *ob = (OllamaBackend *)b->userdata;
    char url[1024];
    snprintf(url, sizeof(url), "%s/api/tags", ob->host);

    HttpResponse resp;
    int rc = http_get(url, &resp, NULL);
    if (rc != 0 || resp.status != 200 || !resp.body) {
        log_error("Failed to list Ollama models from %s (rc=%d, status=%ld, err=%s)",
                  url, rc, resp.status, resp.error);
        http_response_free(&resp);
        return -1;
    }

    cJSON *root = cJSON_ParseWithLength(resp.body, resp.body_len);
    http_response_free(&resp);
    if (!root) {
        log_error("Invalid JSON from %s", url);
        return -1;
    }

    const cJSON *models = cJSON_GetObjectItemCaseSensitive(root, "models");
    if (!models || !cJSON_IsArray(models)) {
        cJSON_Delete(root);
        return -1;
    }

    int count = cJSON_GetArraySize(models);
    if (count > 0) {
        out->items = calloc((size_t)count, sizeof(ModelInfo));
        if (!out->items) {
            cJSON_Delete(root);
            return -1;
        }
        size_t valid = 0;
        for (int i = 0; i < count; i++) {
            const cJSON *m = cJSON_GetArrayItem(models, i);
            const cJSON *name = cJSON_GetObjectItemCaseSensitive(m, "name");
            if (name && cJSON_IsString(name) && name->valuestring) {
                out->items[valid].name = strdup(name->valuestring);
                if (out->items[valid].name) {
                    valid++;
                }
            }
        }
        out->count = valid;
    }

    cJSON_Delete(root);
    return 0;
}

static int ollama_start(Backend *b, const char *model) {
    (void)b;
    (void)model;
    /* Ollama is managed externally or already running */
    return 0;
}

typedef struct StreamState {
    StreamCb cb;
    void *userdata;
    SseParser parser;
} StreamState;

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
            log_error("Ollama stream error: %s", err_msg);
        }
    }
    free(delta);
    free(err_msg);
}

static int on_http_data(const char *data, size_t len, void *userdata) {
    StreamState *st = (StreamState *)userdata;
    return sse_feed(&st->parser, data, len);
}

static int ollama_chat_stream(Backend *b, const Request *request, StreamCb cb, void *userdata) {
    if (!b || !b->userdata || !request || !cb) {
        return -1;
    }

    OllamaBackend *ob = (OllamaBackend *)b->userdata;
    char url[1024];
    snprintf(url, sizeof(url), "%s/v1/chat/completions", ob->host);

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
        log_warn("Ollama chat stream finished with rc=%d, status=%ld, err=%s",
                 rc, resp.status, resp.error);
    }
    http_response_free(&resp);

    return rc;
}

static void ollama_stop(Backend *b) {
    if (!b) {
        return;
    }
    if (b->userdata) {
        OllamaBackend *ob = (OllamaBackend *)b->userdata;
        free(ob->host);
        free(ob);
        b->userdata = NULL;
    }
}

Backend *backend_ollama_new(const char *host) {
    Backend *b = calloc(1, sizeof(Backend));
    if (!b) {
        return NULL;
    }

    OllamaBackend *ob = calloc(1, sizeof(OllamaBackend));
    if (!ob) {
        free(b);
        return NULL;
    }

    const char *h = (host && host[0]) ? host : "http://127.0.0.1:11434";
    size_t hlen = strlen(h);
    while (hlen > 0 && h[hlen - 1] == '/') {
        hlen--;
    }
    ob->host = malloc(hlen + 1);
    if (!ob->host) {
        free(ob);
        free(b);
        return NULL;
    }
    memcpy(ob->host, h, hlen);
    ob->host[hlen] = '\0';

    b->name = "ollama";
    b->userdata = ob;
    b->list_models = ollama_list_models;
    b->start = ollama_start;
    b->chat_stream = ollama_chat_stream;
    b->stop = ollama_stop;

    return b;
}
