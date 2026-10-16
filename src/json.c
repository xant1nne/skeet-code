#include "json.h"
#include "str.h"

#include <stdlib.h>
#include <string.h>

static char *strndup_local(const char *s, size_t n) {
    char *res = malloc(n + 1);
    if (!res) {
        return NULL;
    }
    if (n > 0 && s) {
        memcpy(res, s, n);
    }
    res[n] = '\0';
    return res;
}

const cJSON *json_obj(const cJSON *obj, const char *key) {
    if (!obj || !key || !cJSON_IsObject(obj)) {
        return NULL;
    }
    return cJSON_GetObjectItem(obj, key);
}

const char *json_obj_str(const cJSON *obj, const char *key) {
    const cJSON *item = json_obj(obj, key);
    if (!item || !cJSON_IsString(item) || !item->valuestring) {
        return NULL;
    }
    return item->valuestring;
}

const cJSON *json_obj_arr(const cJSON *obj, const char *key) {
    const cJSON *item = json_obj(obj, key);
    if (!item || !cJSON_IsArray(item)) {
        return NULL;
    }
    return item;
}

int json_arr_len(const cJSON *arr) {
    if (!arr || !cJSON_IsArray(arr)) {
        return 0;
    }
    return cJSON_GetArraySize(arr);
}

const cJSON *json_arr_at(const cJSON *arr, int index) {
    if (!arr || !cJSON_IsArray(arr) || index < 0) {
        return NULL;
    }
    return cJSON_GetArrayItem(arr, index);
}

char *json_chat_body(const Request *request) {
    if (!request) {
        return NULL;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return NULL;
    }

    char *model_str = strndup_local(request->model.ptr, request->model.len);
    if (model_str) {
        cJSON_AddStringToObject(root, "model", model_str);
        free(model_str);
    }

    cJSON *messages_arr = cJSON_AddArrayToObject(root, "messages");
    if (messages_arr && request->messages) {
        for (size_t i = 0; i < request->message_count; i++) {
            const Message *m = &request->messages[i];
            cJSON *msg_obj = cJSON_CreateObject();
            if (!msg_obj) {
                continue;
            }

            char *role_str = strndup_local(m->role.ptr, m->role.len);
            if (role_str) {
                cJSON_AddStringToObject(msg_obj, "role", role_str);
                free(role_str);
            }

            if (m->content.ptr) {
                char *content_str = strndup_local(m->content.ptr, m->content.len);
                if (content_str) {
                    cJSON_AddStringToObject(msg_obj, "content", content_str);
                    free(content_str);
                }
            } else {
                cJSON_AddNullToObject(msg_obj, "content");
            }

            if (m->tool_call_id.ptr && m->tool_call_id.len > 0) {
                char *tcid = strndup_local(m->tool_call_id.ptr, m->tool_call_id.len);
                if (tcid) {
                    cJSON_AddStringToObject(msg_obj, "tool_call_id", tcid);
                    free(tcid);
                }
            }

            if (m->tool_calls_json.ptr && m->tool_calls_json.len > 0) {
                char *tc_raw = strndup_local(m->tool_calls_json.ptr, m->tool_calls_json.len);
                if (tc_raw) {
                    cJSON *parsed_tcs = cJSON_Parse(tc_raw);
                    if (parsed_tcs) {
                        cJSON_AddItemToObject(msg_obj, "tool_calls", parsed_tcs);
                    }
                    free(tc_raw);
                }
            }

            cJSON_AddItemToArray(messages_arr, msg_obj);
        }
    }

    if (request->tools_json.ptr && request->tools_json.len > 0) {
        char *tools_raw = strndup_local(request->tools_json.ptr, request->tools_json.len);
        if (tools_raw) {
            cJSON *parsed_tools = cJSON_Parse(tools_raw);
            if (parsed_tools) {
                cJSON_AddItemToObject(root, "tools", parsed_tools);
            }
            free(tools_raw);
        }
    }

    cJSON_AddBoolToObject(root, "stream", 1);

    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}

int json_chat_delta(const char *data, size_t len, char **text, char **err_msg) {
    if (text) {
        *text = NULL;
    }
    if (err_msg) {
        *err_msg = NULL;
    }

    if (!data || len == 0) {
        return -1;
    }

    char *buf = strndup_local(data, len);
    if (!buf) {
        return -1;
    }

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
                if (content && text) {
                    *text = strdup(content);
                }
            }
        }
    }

    cJSON_Delete(root);
    return 0;
}
