#ifndef SKEET_TOOL_REGISTRY_H
#define SKEET_TOOL_REGISTRY_H

#include "arena.h"
#include "str.h"
#include <stdint.h>
#include <stddef.h>

#define TOOL_READONLY (1 << 0)
#define TOOL_NEEDS_CONFIRM (1 << 1)

typedef struct SkeetCtx SkeetCtx;

typedef struct ToolCall {
    Str id;
    Str name;
    Str args_json;
} ToolCall;

typedef struct ToolResult {
    int is_error;
    Str content;
} ToolResult;

typedef struct ToolDef {
    uint32_t size;
    const char *name;
    const char *description;
    const char *schema_json;
    uint32_t flags;
    int (*run)(SkeetCtx *ctx, const ToolCall *call, ToolResult *result, void *userdata);
    void *userdata;
    const char *owner;
} ToolDef;

typedef struct RegisteredTool {
    ToolDef def;
    char *name;
    char *description;
    char *schema_json;
    char *owner;
} RegisteredTool;

typedef struct ToolRegistry {
    RegisteredTool *items;
    size_t count;
    size_t cap;
} ToolRegistry;

ToolRegistry *tool_registry_new(void);
void tool_registry_free(ToolRegistry *reg);
int tool_register(SkeetCtx *ctx, const ToolDef *def);
int tool_unregister_owner(SkeetCtx *ctx, const char *owner);
const ToolDef *tool_find(SkeetCtx *ctx, const char *name);
size_t tool_registry_count(SkeetCtx *ctx);
const ToolDef *tool_registry_at(SkeetCtx *ctx, size_t index);
int tools_build_request_json(SkeetCtx *ctx, Str *out_json);

#endif
