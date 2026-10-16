#include "tools/accumulator.h"
#include "json.h"
#include "cJSON.h"

#include <stdlib.h>
#include <string.h>

void accumulator_init(ToolCallAccumulator *acc) {
    if (!acc) {
        return;
    }
    memset(acc, 0, sizeof(*acc));
}

void accumulator_free(ToolCallAccumulator *acc) {
    if (!acc) {
        return;
    }
    for (size_t i = 0; i < acc->count; i++) {
        free(acc->calls[i].id);
        free(acc->calls[i].name);
        strbuf_free(&acc->calls[i].args_buf);
    }
    free(acc->calls);
    memset(acc, 0, sizeof(*acc));
}

static AccumulatedToolCall *find_or_create_call(ToolCallAccumulator *acc, int index) {
    for (size_t i = 0; i < acc->count; i++) {
        if (acc->calls[i].index == index) {
            return &acc->calls[i];
        }
    }

    if (acc->count >= acc->cap) {
        size_t new_cap = acc->cap ? acc->cap * 2 : 4;
        AccumulatedToolCall *new_calls = realloc(acc->calls, new_cap * sizeof(AccumulatedToolCall));
        if (!new_calls) {
            return NULL;
        }
        acc->calls = new_calls;
        acc->cap = new_cap;
    }

    AccumulatedToolCall *call = &acc->calls[acc->count++];
    memset(call, 0, sizeof(*call));
    call->index = index;
    strbuf_init(&call->args_buf);
    return call;
}

int accumulator_feed_chunk(ToolCallAccumulator *acc, int index, const char *id, const char *name, const char *args_chunk) {
    if (!acc) {
        return -1;
    }

    AccumulatedToolCall *call = find_or_create_call(acc, index);
    if (!call) {
        return -1;
    }

    if (id && !call->id) {
        call->id = strdup(id);
    }
    if (name && !call->name) {
        call->name = strdup(name);
    }
    if (args_chunk && args_chunk[0]) {
        strbuf_append(&call->args_buf, args_chunk, strlen(args_chunk));
    }
    return 0;
}

int accumulator_feed_sse_json(ToolCallAccumulator *acc, const char *data, size_t len, char **text_chunk, char **err_msg) {
    if (text_chunk) {
        *text_chunk = NULL;
    }
    if (err_msg) {
        *err_msg = NULL;
    }

    if (!acc || !data || len == 0) {
        return -1;
    }

    char *buf = malloc(len + 1);
    if (!buf) {
        return -1;
    }
    memcpy(buf, data, len);
    buf[len] = '\0';

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) {
        return -1;
    }

    const cJSON *err = json_obj(root, "error");
    if (err) {
        const char *msg = json_obj_str(err, "message");
        if (msg && err_msg) {
            *err_msg = strdup(msg);
        }
        cJSON_Delete(root);
        return 0;
    }

    const cJSON *choices = json_obj_arr(root, "choices");
    if (choices) {
        const cJSON *first = json_arr_at(choices, 0);
        if (first) {
            const cJSON *delta = json_obj(first, "delta");
            if (delta) {
                const char *content = json_obj_str(delta, "content");
                if (content && content[0] && text_chunk) {
                    *text_chunk = strdup(content);
                }

                const cJSON *tool_calls = json_obj_arr(delta, "tool_calls");
                if (tool_calls) {
                    int num_tc = json_arr_len(tool_calls);
                    for (int i = 0; i < num_tc; i++) {
                        const cJSON *tc = json_arr_at(tool_calls, i);
                        if (!tc) {
                            continue;
                        }

                        int idx = 0;
                        const cJSON *idx_obj = json_obj(tc, "index");
                        if (idx_obj && cJSON_IsNumber(idx_obj)) {
                            idx = idx_obj->valueint;
                        }

                        const char *id = json_obj_str(tc, "id");
                        const cJSON *fn_obj = json_obj(tc, "function");
                        const char *name = NULL;
                        const char *args = NULL;
                        if (fn_obj) {
                            name = json_obj_str(fn_obj, "name");
                            args = json_obj_str(fn_obj, "arguments");
                        }

                        accumulator_feed_chunk(acc, idx, id, name, args);
                    }
                }
            }
        }
    }

    cJSON_Delete(root);
    return 0;
}

size_t accumulator_get_calls(ToolCallAccumulator *acc, ToolCall **out_calls, Arena *arena) {
    if (!acc || acc->count == 0 || !out_calls || !arena) {
        if (out_calls) {
            *out_calls = NULL;
        }
        return 0;
    }

    ToolCall *calls = arena_alloc(arena, acc->count * sizeof(ToolCall));
    if (!calls) {
        *out_calls = NULL;
        return 0;
    }

    for (size_t i = 0; i < acc->count; i++) {
        const AccumulatedToolCall *src = &acc->calls[i];
        const char *id_str = src->id ? src->id : "";
        size_t id_len = strlen(id_str);
        char *id_arena = arena_strndup(arena, id_str, id_len);

        const char *name_str = src->name ? src->name : "";
        size_t name_len = strlen(name_str);
        char *name_arena = arena_strndup(arena, name_str, name_len);

        const char *args_str = src->args_buf.data ? src->args_buf.data : "{}";
        size_t args_len = src->args_buf.len > 0 ? src->args_buf.len : strlen(args_str);
        char *args_arena = arena_strndup(arena, args_str, args_len);

        calls[i].id = (Str){ .ptr = id_arena, .len = id_len };
        calls[i].name = (Str){ .ptr = name_arena, .len = name_len };
        calls[i].args_json = (Str){ .ptr = args_arena, .len = args_len };
    }

    *out_calls = calls;
    return acc->count;
}

char *accumulator_to_json(const ToolCallAccumulator *acc) {
    if (!acc || acc->count == 0) {
        return NULL;
    }

    cJSON *arr = cJSON_CreateArray();
    if (!arr) {
        return NULL;
    }

    for (size_t i = 0; i < acc->count; i++) {
        const AccumulatedToolCall *c = &acc->calls[i];
        cJSON *obj = cJSON_CreateObject();
        if (!obj) {
            continue;
        }
        if (c->id) {
            cJSON_AddStringToObject(obj, "id", c->id);
        }
        cJSON_AddStringToObject(obj, "type", "function");
        cJSON *fn = cJSON_CreateObject();
        if (fn) {
            if (c->name) {
                cJSON_AddStringToObject(fn, "name", c->name);
            }
            cJSON_AddStringToObject(fn, "arguments", c->args_buf.data ? c->args_buf.data : "{}");
            cJSON_AddItemToObject(obj, "function", fn);
        }
        cJSON_AddItemToArray(arr, obj);
    }

    char *res = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
    return res;
}
