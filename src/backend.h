#ifndef SKEET_BACKEND_H
#define SKEET_BACKEND_H

#include "str.h"

#include <signal.h>
#include <stddef.h>

/* Return codes for start, list_models, and chat_stream:
 *  0  ok
 *  1  cancelled
 * -1  error
 */

typedef struct ModelInfo {
    char *name;
} ModelInfo;

typedef struct ModelList {
    ModelInfo *items;
    size_t count;
} ModelList;

typedef struct Message {
    Str role;
    Str content;
    Str tool_call_id;
    Str tool_calls_json;
} Message;

typedef struct Request {
    Str model;
    const Message *messages;
    size_t message_count;
    Str tools_json;
} Request;

/* Called for each content fragment. data is not necessarily NUL-terminated. */
typedef void (*StreamCb)(const char *data, size_t len, void *userdata);

typedef struct Backend Backend;

struct Backend {
    const char *name;
    void *userdata;
    volatile sig_atomic_t *cancel;
    int (*list_models)(Backend *backend, ModelList *out);
    int (*start)(Backend *backend, const char *model);
    int (*chat_stream)(Backend *backend, const Request *request, StreamCb cb, void *userdata);
    void (*stop)(Backend *backend);
};

void model_list_free(ModelList *list);

/* Host is copied. NULL host becomes http://127.0.0.1:11434. */
Backend *backend_ollama_new(const char *host);
Backend *backend_llama_new(const char *server_path, const char *model_path, const char *host, int port, const char *custom_flags);
void backend_free(Backend *backend);

#endif
