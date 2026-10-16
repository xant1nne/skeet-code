#ifndef SKEET_PERMISSIONS_H
#define SKEET_PERMISSIONS_H

#include "tools/registry.h"
#include <stddef.h>

typedef struct SkeetCtx SkeetCtx;

typedef enum {
    PERM_MODE_ASK = 0,
    PERM_MODE_YES = 1,
    PERM_MODE_READONLY = 2
} PermMode;

typedef enum {
    PERM_ALLOW = 0,
    PERM_ASK = 1,
    PERM_DENY = 2
} PermAction;

typedef int (*PermConfirmFn)(SkeetCtx *ctx, const ToolCall *call, const char *preview, void *userdata);

typedef struct PermissionPolicy PermissionPolicy;

PermissionPolicy *permissions_new(PermMode mode);
void permissions_free(PermissionPolicy *p);
void perm_set_mode(SkeetCtx *ctx, PermMode mode);
PermMode perm_get_mode(SkeetCtx *ctx);
void perm_set_tool_action(SkeetCtx *ctx, const char *tool_name, PermAction action);
PermAction perm_get_tool_action(SkeetCtx *ctx, const char *tool_name);
void perm_set_confirm_fn(SkeetCtx *ctx, PermConfirmFn fn, void *userdata);
int perm_check(SkeetCtx *ctx, const ToolCall *call, const ToolDef *def, const char *preview);

#endif
