#include "tools/text_fallback.h"
#include "cJSON.h"
#include "json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int parse_single_call_json(const char *json_text, size_t json_len, ToolCall *call, Arena *arena, size_t call_idx) {
    char *buf = malloc(json_len + 1);
    if (!buf) {
        return -1;
    }
    memcpy(buf, json_text, json_len);
    buf[json_len] = '\0';

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) {
        return -1;
    }

    const char *name = json_obj_str(root, "name");
    if (!name) {
        const cJSON *fn = json_obj(root, "function");
        if (fn) {
            name = json_obj_str(fn, "name");
        }
    }

    if (!name) {
        cJSON_Delete(root);
        return -1;
    }

    char id_buf[64];
    const char *id = json_obj_str(root, "id");
    if (!id) {
        snprintf(id_buf, sizeof(id_buf), "call_fallback_%zu", call_idx);
        id = id_buf;
    }

    char *args_str = NULL;
    const cJSON *args_obj = json_obj(root, "arguments");
    if (!args_obj) {
        args_obj = json_obj(root, "parameters");
    }

    if (args_obj) {
        if (cJSON_IsString(args_obj) && args_obj->valuestring) {
            args_str = strdup(args_obj->valuestring);
        } else {
            args_str = cJSON_PrintUnformatted(args_obj);
        }
    } else {
        args_str = strdup("{}");
    }

    cJSON_Delete(root);

    if (!args_str) {
        return -1;
    }

    size_t id_len = strlen(id);
    char *id_arena = arena_strndup(arena, id, id_len);

    size_t name_len = strlen(name);
    char *name_arena = arena_strndup(arena, name, name_len);

    size_t args_len = strlen(args_str);
    char *args_arena = arena_strndup(arena, args_str, args_len);
    free(args_str);

    if (!id_arena || !name_arena || !args_arena) {
        return -1;
    }

    call->id = (Str){ .ptr = id_arena, .len = id_len };
    call->name = (Str){ .ptr = name_arena, .len = name_len };
    call->args_json = (Str){ .ptr = args_arena, .len = args_len };
    return 0;
}

size_t text_parse_fallback_tool_calls(const char *text, size_t len, ToolCall **out_calls, Arena *arena) {
    if (!text || len == 0 || !out_calls || !arena) {
        if (out_calls) {
            *out_calls = NULL;
        }
        return 0;
    }

    const char *start_tag = "<tool_call>";
    const char *end_tag = "</tool_call>";
    size_t start_len = strlen(start_tag);
    size_t end_len = strlen(end_tag);

    ToolCall temp_calls[32];
    size_t count = 0;

    const char *cur = text;
    const char *end = text + len;

    while (cur < end && count < 32) {
        const char *p_start = strstr(cur, start_tag);
        if (!p_start || p_start >= end) {
            break;
        }
        const char *body_start = p_start + start_len;
        const char *p_end = strstr(body_start, end_tag);
        if (!p_end || p_end > end) {
            break;
        }

        size_t json_len = (size_t)(p_end - body_start);
        if (parse_single_call_json(body_start, json_len, &temp_calls[count], arena, count) == 0) {
            count++;
        }
        cur = p_end + end_len;
    }

    if (count == 0) {
        *out_calls = NULL;
        return 0;
    }

    ToolCall *res = arena_alloc(arena, count * sizeof(ToolCall));
    if (!res) {
        *out_calls = NULL;
        return 0;
    }
    memcpy(res, temp_calls, count * sizeof(ToolCall));
    *out_calls = res;
    return count;
}
