#include "tools/registry.h"
#include "context.h"
#include "cJSON.h"
#include "log.h"

#include <stdlib.h>
#include <string.h>

static void free_tool_item(RegisteredTool *item) {
    if (!item) {
        return;
    }
    free(item->name);
    free(item->description);
    free(item->schema_json);
    free(item->owner);
}

ToolRegistry *tool_registry_new(void) {
    ToolRegistry *reg = calloc(1, sizeof(ToolRegistry));
    if (!reg) {
        return NULL;
    }
    reg->cap = 8;
    reg->items = calloc(reg->cap, sizeof(RegisteredTool));
    if (!reg->items) {
        free(reg);
        return NULL;
    }
    return reg;
}

void tool_registry_free(ToolRegistry *reg) {
    if (!reg) {
        return;
    }
    for (size_t i = 0; i < reg->count; i++) {
        free_tool_item(&reg->items[i]);
    }
    free(reg->items);
    free(reg);
}

int tool_register(SkeetCtx *ctx, const ToolDef *def) {
    if (!ctx || !ctx->tools || !def || !def->name) {
        return -1;
    }

    ToolRegistry *reg = ctx->tools;
    for (size_t i = 0; i < reg->count; i++) {
        if (strcmp(reg->items[i].name, def->name) == 0) {
            free_tool_item(&reg->items[i]);
            reg->items[i].def = *def;
            reg->items[i].name = strdup(def->name);
            reg->items[i].description = def->description ? strdup(def->description) : NULL;
            reg->items[i].schema_json = def->schema_json ? strdup(def->schema_json) : NULL;
            reg->items[i].owner = def->owner ? strdup(def->owner) : NULL;
            reg->items[i].def.name = reg->items[i].name;
            reg->items[i].def.description = reg->items[i].description;
            reg->items[i].def.schema_json = reg->items[i].schema_json;
            reg->items[i].def.owner = reg->items[i].owner;
            return 0;
        }
    }

    if (reg->count >= reg->cap) {
        size_t new_cap = reg->cap * 2;
        RegisteredTool *new_items = realloc(reg->items, new_cap * sizeof(RegisteredTool));
        if (!new_items) {
            return -1;
        }
        reg->items = new_items;
        reg->cap = new_cap;
    }

    RegisteredTool *item = &reg->items[reg->count++];
    memset(item, 0, sizeof(*item));
    item->def = *def;
    item->name = strdup(def->name);
    item->description = def->description ? strdup(def->description) : NULL;
    item->schema_json = def->schema_json ? strdup(def->schema_json) : NULL;
    item->owner = def->owner ? strdup(def->owner) : NULL;
    item->def.name = item->name;
    item->def.description = item->description;
    item->def.schema_json = item->schema_json;
    item->def.owner = item->owner;

    return 0;
}

int tool_unregister_owner(SkeetCtx *ctx, const char *owner) {
    if (!ctx || !ctx->tools || !owner) {
        return 0;
    }
    ToolRegistry *reg = ctx->tools;
    size_t removed = 0;
    size_t write_idx = 0;
    for (size_t i = 0; i < reg->count; i++) {
        if (reg->items[i].owner && strcmp(reg->items[i].owner, owner) == 0) {
            free_tool_item(&reg->items[i]);
            removed++;
        } else {
            if (write_idx != i) {
                reg->items[write_idx] = reg->items[i];
            }
            write_idx++;
        }
    }
    reg->count = write_idx;
    return (int)removed;
}

const ToolDef *tool_find(SkeetCtx *ctx, const char *name) {
    if (!ctx || !ctx->tools || !name) {
        return NULL;
    }
    ToolRegistry *reg = ctx->tools;
    for (size_t i = 0; i < reg->count; i++) {
        if (strcmp(reg->items[i].name, name) == 0) {
            return &reg->items[i].def;
        }
    }
    return NULL;
}

size_t tool_registry_count(SkeetCtx *ctx) {
    if (!ctx || !ctx->tools) {
        return 0;
    }
    return ctx->tools->count;
}

const ToolDef *tool_registry_at(SkeetCtx *ctx, size_t index) {
    if (!ctx || !ctx->tools || index >= ctx->tools->count) {
        return NULL;
    }
    return &ctx->tools->items[index].def;
}

int tools_build_request_json(SkeetCtx *ctx, Str *out_json) {
    if (!ctx || !ctx->tools || !out_json) {
        return -1;
    }

    ToolRegistry *reg = ctx->tools;
    if (reg->count == 0) {
        *out_json = (Str){ .ptr = NULL, .len = 0 };
        return 0;
    }

    cJSON *arr = cJSON_CreateArray();
    if (!arr) {
        return -1;
    }

    for (size_t i = 0; i < reg->count; i++) {
        const ToolDef *td = &reg->items[i].def;
        cJSON *tool_obj = cJSON_CreateObject();
        if (!tool_obj) {
            continue;
        }
        cJSON_AddStringToObject(tool_obj, "type", "function");
        cJSON *fn_obj = cJSON_CreateObject();
        if (!fn_obj) {
            cJSON_Delete(tool_obj);
            continue;
        }
        cJSON_AddStringToObject(fn_obj, "name", td->name);
        if (td->description) {
            cJSON_AddStringToObject(fn_obj, "description", td->description);
        }
        if (td->schema_json) {
            cJSON *param_obj = cJSON_Parse(td->schema_json);
            if (param_obj) {
                cJSON_AddItemToObject(fn_obj, "parameters", param_obj);
            }
        }
        cJSON_AddItemToObject(tool_obj, "function", fn_obj);
        cJSON_AddItemToArray(arr, tool_obj);
    }

    char *json_str = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
    if (!json_str) {
        return -1;
    }

    size_t len = strlen(json_str);
    char *arena_copy = arena_strndup(&ctx->turn_arena, json_str, len);
    free(json_str);

    if (!arena_copy) {
        return -1;
    }

    out_json->ptr = arena_copy;
    out_json->len = len;
    return 0;
}
