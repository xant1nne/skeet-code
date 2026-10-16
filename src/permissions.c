#include "permissions.h"
#include "context.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct ToolOverride {
    char *tool_name;
    PermAction action;
} ToolOverride;

struct PermissionPolicy {
    PermMode mode;
    ToolOverride *overrides;
    size_t count;
    size_t cap;
    PermConfirmFn confirm_fn;
    void *confirm_userdata;
};

static int default_confirm_fn(SkeetCtx *ctx, const ToolCall *call, const char *preview, void *userdata) {
    (void)ctx;
    (void)userdata;
    char name_buf[128];
    size_t nlen = call->name.len < sizeof(name_buf) - 1 ? call->name.len : sizeof(name_buf) - 1;
    memcpy(name_buf, call->name.ptr, nlen);
    name_buf[nlen] = '\0';

    printf("\n\033[1;33m[confirm]\033[0m Run tool '\033[1m%s\033[0m'?\n", name_buf);
    if (preview && preview[0]) {
        printf("%s\n", preview);
    }
    printf("Allow [y/n/a] (yes / no / always in this session)? ");
    fflush(stdout);

    char ans[64];
    if (!fgets(ans, sizeof(ans), stdin)) {
        printf("\n");
        return 0;
    }

    if (ans[0] == 'y' || ans[0] == 'Y') {
        return 1;
    }
    if (ans[0] == 'a' || ans[0] == 'A') {
        return 2;
    }
    return 0;
}

PermissionPolicy *permissions_new(PermMode mode) {
    PermissionPolicy *p = calloc(1, sizeof(PermissionPolicy));
    if (!p) {
        return NULL;
    }
    p->mode = mode;
    p->confirm_fn = default_confirm_fn;
    return p;
}

void permissions_free(PermissionPolicy *p) {
    if (!p) {
        return;
    }
    for (size_t i = 0; i < p->count; i++) {
        free(p->overrides[i].tool_name);
    }
    free(p->overrides);
    free(p);
}

void perm_set_mode(SkeetCtx *ctx, PermMode mode) {
    if (ctx && ctx->perms) {
        ctx->perms->mode = mode;
    }
}

PermMode perm_get_mode(SkeetCtx *ctx) {
    return (ctx && ctx->perms) ? ctx->perms->mode : PERM_MODE_ASK;
}

void perm_set_tool_action(SkeetCtx *ctx, const char *tool_name, PermAction action) {
    if (!ctx || !ctx->perms || !tool_name) {
        return;
    }
    PermissionPolicy *p = ctx->perms;
    for (size_t i = 0; i < p->count; i++) {
        if (strcmp(p->overrides[i].tool_name, tool_name) == 0) {
            p->overrides[i].action = action;
            return;
        }
    }

    if (p->count + 1 > p->cap) {
        size_t new_cap = p->cap ? p->cap * 2 : 8;
        ToolOverride *new_items = realloc(p->overrides, new_cap * sizeof(ToolOverride));
        if (!new_items) {
            return;
        }
        p->overrides = new_items;
        p->cap = new_cap;
    }

    p->overrides[p->count].tool_name = strdup(tool_name);
    p->overrides[p->count].action = action;
    p->count++;
}

PermAction perm_get_tool_action(SkeetCtx *ctx, const char *tool_name) {
    if (!ctx || !ctx->perms || !tool_name) {
        return PERM_ASK;
    }
    PermissionPolicy *p = ctx->perms;
    for (size_t i = 0; i < p->count; i++) {
        if (strcmp(p->overrides[i].tool_name, tool_name) == 0) {
            return p->overrides[i].action;
        }
    }
    return PERM_ASK;
}

void perm_set_confirm_fn(SkeetCtx *ctx, PermConfirmFn fn, void *userdata) {
    if (ctx && ctx->perms) {
        ctx->perms->confirm_fn = fn ? fn : default_confirm_fn;
        ctx->perms->confirm_userdata = userdata;
    }
}

int perm_check(SkeetCtx *ctx, const ToolCall *call, const ToolDef *def, const char *preview) {
    if (!ctx || !ctx->perms || !def) {
        return 1;
    }
    PermissionPolicy *p = ctx->perms;

    if (p->mode == PERM_MODE_YES) {
        return 1;
    }

    if (p->mode == PERM_MODE_READONLY && !(def->flags & TOOL_READONLY)) {
        log_info("Denied tool '%s' due to readonly mode", def->name);
        return 0;
    }

    char name_buf[128];
    size_t nlen = call->name.len < sizeof(name_buf) - 1 ? call->name.len : sizeof(name_buf) - 1;
    memcpy(name_buf, call->name.ptr, nlen);
    name_buf[nlen] = '\0';

    for (size_t i = 0; i < p->count; i++) {
        if (strcmp(p->overrides[i].tool_name, name_buf) == 0) {
            if (p->overrides[i].action == PERM_ALLOW) {
                return 1;
            }
            if (p->overrides[i].action == PERM_DENY) {
                return 0;
            }
            break;
        }
    }

    if (!(def->flags & TOOL_NEEDS_CONFIRM)) {
        return 1;
    }

    if (!p->confirm_fn) {
        return 0;
    }

    int res = p->confirm_fn(ctx, call, preview, p->confirm_userdata);
    if (res == 2) {
        perm_set_tool_action(ctx, name_buf, PERM_ALLOW);
        return 1;
    }
    return res == 1 ? 1 : 0;
}
