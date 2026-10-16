#include "context.h"
#include "commands.h"
#include "compat.h"
#include "events.h"
#include "permissions.h"
#include "tools/registry.h"

#include <stdlib.h>
#include <string.h>

struct ReadFilesSet {
    char **paths;
    size_t count;
    size_t cap;
};

static ReadFilesSet *read_files_new(void) {
    ReadFilesSet *set = calloc(1, sizeof(ReadFilesSet));
    if (!set) {
        return NULL;
    }
    set->cap = 16;
    set->paths = calloc(set->cap, sizeof(char *));
    if (!set->paths) {
        free(set);
        return NULL;
    }
    return set;
}

static void read_files_free(ReadFilesSet *set) {
    if (!set) {
        return;
    }
    for (size_t i = 0; i < set->count; i++) {
        free(set->paths[i]);
    }
    free(set->paths);
    free(set);
}

void skeet_mark_file_read(SkeetCtx *ctx, const char *path) {
    if (!ctx || !ctx->read_files || !path) {
        return;
    }

    char *resolved = skeet_resolve_path(ctx->workspace_root, path);
    const char *to_store = resolved ? resolved : path;

    ReadFilesSet *set = ctx->read_files;
    for (size_t i = 0; i < set->count; i++) {
        if (strcmp(set->paths[i], to_store) == 0) {
            free(resolved);
            return;
        }
    }

    if (set->count >= set->cap) {
        size_t new_cap = set->cap * 2;
        char **new_paths = realloc(set->paths, new_cap * sizeof(char *));
        if (!new_paths) {
            free(resolved);
            return;
        }
        set->paths = new_paths;
        set->cap = new_cap;
    }

    set->paths[set->count++] = strdup(to_store);
    free(resolved);
}

int skeet_is_file_read(SkeetCtx *ctx, const char *path) {
    if (!ctx || !ctx->read_files || !path) {
        return 0;
    }

    char *resolved = skeet_resolve_path(ctx->workspace_root, path);
    const char *to_check = resolved ? resolved : path;

    ReadFilesSet *set = ctx->read_files;
    for (size_t i = 0; i < set->count; i++) {
        if (strcmp(set->paths[i], to_check) == 0) {
            free(resolved);
            return 1;
        }
    }

    free(resolved);
    return 0;
}

SkeetCtx *skeet_ctx_new(Config *config, Backend *backend, volatile sig_atomic_t *cancel, const char *workspace_root) {
    SkeetCtx *ctx = calloc(1, sizeof(SkeetCtx));
    if (!ctx) {
        return NULL;
    }

    ctx->abi_version = SKEET_ABI_VERSION;
    arena_init(&ctx->perm_arena, 64 * 1024);
    arena_init(&ctx->turn_arena, 64 * 1024);

    ctx->config = config;
    ctx->backend = backend;
    ctx->cancel = cancel;

    ctx->tools = tool_registry_new();
    ctx->events = event_bus_new();
    ctx->commands = command_registry_new();
    ctx->perms = permissions_new(PERM_MODE_ASK);
    ctx->read_files = read_files_new();

    const char *ws = workspace_root ? workspace_root : ".";
    char *abs_ws = skeet_realpath(ws);
    ctx->workspace_root = abs_ws ? abs_ws : strdup(ws);

    ctx->history_cap = 64;
    ctx->history = calloc(ctx->history_cap, sizeof(Message));

    commands_register_builtins(ctx);

    return ctx;
}

void skeet_ctx_free(SkeetCtx *ctx) {
    if (!ctx) {
        return;
    }

    for (size_t i = 0; i < ctx->history_count; i++) {
        free((char *)ctx->history[i].role.ptr);
        free((char *)ctx->history[i].content.ptr);
        free((char *)ctx->history[i].tool_call_id.ptr);
        free((char *)ctx->history[i].tool_calls_json.ptr);
    }
    free(ctx->history);

    read_files_free(ctx->read_files);
    permissions_free(ctx->perms);
    command_registry_free(ctx->commands);
    event_bus_free(ctx->events);
    tool_registry_free(ctx->tools);

    free(ctx->workspace_root);

    arena_free(&ctx->turn_arena);
    arena_free(&ctx->perm_arena);

    free(ctx);
}

int skeet_pump(SkeetCtx *ctx) {
    if (!ctx || !ctx->pump) {
        return 0;
    }
    return ctx->pump(ctx, ctx->pump_userdata);
}
