#ifndef SKEET_CONTEXT_H
#define SKEET_CONTEXT_H

#include "arena.h"
#include "backend.h"
#include "config.h"
#include "str.h"

#include <signal.h>
#include <stdint.h>
#include <stddef.h>

#define SKEET_ABI_VERSION 1

typedef struct ToolRegistry ToolRegistry;
typedef struct EventBus EventBus;
typedef struct CommandRegistry CommandRegistry;
typedef struct PermissionPolicy PermissionPolicy;
typedef struct ReadFilesSet ReadFilesSet;
typedef struct SkeetCtx SkeetCtx;

typedef int (*SkeetPumpFn)(SkeetCtx *ctx, void *userdata);

struct SkeetCtx {
    uint32_t abi_version;
    Arena perm_arena;
    Arena turn_arena;
    Config *config;
    Backend *backend;
    ToolRegistry *tools;
    EventBus *events;
    CommandRegistry *commands;
    PermissionPolicy *perms;
    ReadFilesSet *read_files;
    char *workspace_root;
    SkeetPumpFn pump;
    void *pump_userdata;
    volatile sig_atomic_t *cancel;
    Message *history;
    size_t history_count;
    size_t history_cap;
};

SkeetCtx *skeet_ctx_new(Config *config, Backend *backend, volatile sig_atomic_t *cancel, const char *workspace_root);
void skeet_ctx_free(SkeetCtx *ctx);
void skeet_mark_file_read(SkeetCtx *ctx, const char *path);
int skeet_is_file_read(SkeetCtx *ctx, const char *path);
int skeet_pump(SkeetCtx *ctx);

/* M6 Context & Session Management */
int context_init_system_prompt(SkeetCtx *ctx, const char *model);
size_t context_estimate_tokens(const SkeetCtx *ctx);
int context_compact(SkeetCtx *ctx, size_t max_tokens);

int session_save(SkeetCtx *ctx, const char *session_name);
int session_load(SkeetCtx *ctx, const char *session_name);
int session_list(SkeetCtx *ctx, char ***out_names, size_t *out_count);
void session_list_free(char **names, size_t count);

#endif
