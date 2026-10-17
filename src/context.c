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

/* --- M6: Context & Session Management --- */

#include "cJSON.h"
#include <stdio.h>

#if defined(SKEET_OS_WINDOWS)
    #include <windows.h>
    #include <io.h>
#else
    #include <dirent.h>
    #include <sys/stat.h>
#endif

static char *read_file_text(const char *path, size_t max_bytes) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    if (sz < 0) { fclose(f); return NULL; }
    if ((size_t)sz > (long)max_bytes) sz = (long)max_bytes;
    fseek(f, 0, SEEK_SET);

    char *buf = malloc(sz + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t n = fread(buf, 1, sz, f);
    buf[n] = '\0';
    fclose(f);
    return buf;
}

static void context_ensure_cap(SkeetCtx *ctx, size_t needed) {
    if (ctx->history_count + needed > ctx->history_cap) {
        size_t new_cap = ctx->history_cap ? ctx->history_cap * 2 : 64;
        while (new_cap < ctx->history_count + needed) {
            new_cap *= 2;
        }
        Message *new_h = realloc(ctx->history, new_cap * sizeof(Message));
        if (new_h) {
            ctx->history = new_h;
            ctx->history_cap = new_cap;
        }
    }
}

int context_init_system_prompt(SkeetCtx *ctx, const char *model) {
    if (!ctx) return -1;
    if (ctx->history_count > 0 && ctx->history[0].role.len == 6 &&
        strncmp(ctx->history[0].role.ptr, "system", 6) == 0) {
        return 0; /* Already initialized */
    }

    char skeet_md_path[PATH_MAX];
    snprintf(skeet_md_path, sizeof(skeet_md_path), "%s/SKEET.md", ctx->workspace_root);
    char *skeet_md_text = read_file_text(skeet_md_path, 32 * 1024);

    StrBuf prompt;
    strbuf_init(&prompt);

    strbuf_puts(&prompt,
        "You are Skeet Code, an elite terminal AI software engineer and pair programmer.\n"
        "You operate directly inside the user's repository via the Skeet runtime engine.\n\n"
        "### ENVIRONMENT:\n"
    );
#if defined(SKEET_OS_WINDOWS)
    strbuf_puts(&prompt, "- OS: Windows (PowerShell / cmd / native API)\n");
#elif defined(SKEET_OS_MACOS)
    strbuf_puts(&prompt, "- OS: macOS (Darwin / POSIX)\n");
#else
    strbuf_puts(&prompt, "- OS: Linux (POSIX)\n");
#endif
    strbuf_puts(&prompt, "- Workspace Root: ");
    strbuf_puts(&prompt, ctx->workspace_root);
    strbuf_puts(&prompt, "\n");
    if (model) {
        strbuf_puts(&prompt, "- Model: ");
        strbuf_puts(&prompt, model);
        strbuf_puts(&prompt, "\n");
    }

    if (skeet_md_text && skeet_md_text[0]) {
        strbuf_puts(&prompt, "\n### PROJECT CONTEXT & CONVENTIONS (from SKEET.md):\n");
        strbuf_puts(&prompt, skeet_md_text);
        strbuf_puts(&prompt, "\n------------------------------------------------\n\n");
    }
    free(skeet_md_text);

    strbuf_puts(&prompt,
        "### CORE OPERATIONAL DIRECTIVES:\n"
        "1. BE CONCISE & ON-POINT: No filler, no sycophantic greetings, no repeating the question. Answer directly and act immediately.\n"
        "2. GROUND YOUR WORK IN REALITY: Never guess file paths, symbol names, or APIs. Always inspect files using `read_file`, `grep`, or `glob` before modifying code.\n"
        "3. MINIMAL SURGICAL EDITS: Use `edit_file` or `codex_apply_patch` for targeted, precise changes. Never rewrite an entire file when changing a few lines suffices. Preserve existing comments, formatting, and coding style.\n"
        "4. VERIFY YOUR WORK: After making modifications, run tests or build commands via `bash` to confirm there are no compile errors or broken tests before declaring completion.\n"
        "5. TOOL CALL PROTOCOL: When calling tools, emit valid structured JSON arguments. Do not narrate what you are about to do before calling a tool; simply invoke it.\n"
        "6. SUMMARY EXPLANATION: After tool execution finishes, explain the rationale of the changes in 1-3 crisp sentences.\n"
    );

    char *sys_text = strbuf_detach(&prompt);
    size_t sys_len = sys_text ? strlen(sys_text) : 0;

    context_ensure_cap(ctx, 1);
    /* Shift existing history if any */
    if (ctx->history_count > 0) {
        memmove(&ctx->history[1], &ctx->history[0], ctx->history_count * sizeof(Message));
    }
    ctx->history[0] = (Message){
        .role = str_from_cstr("system"),
        .content = (Str){ .ptr = sys_text, .len = sys_len },
        .tool_call_id = (Str){ .ptr = NULL, .len = 0 },
        .tool_calls_json = (Str){ .ptr = NULL, .len = 0 }
    };
    ctx->history_count++;

    return 0;
}

size_t context_estimate_tokens(const SkeetCtx *ctx) {
    if (!ctx || ctx->history_count == 0) return 0;
    size_t total_chars = 0;
    for (size_t i = 0; i < ctx->history_count; i++) {
        const Message *m = &ctx->history[i];
        total_chars += m->role.len;
        total_chars += m->content.len;
        total_chars += m->tool_call_id.len;
        total_chars += m->tool_calls_json.len;
        total_chars += 16; /* Overhead per message */
    }
    return (total_chars + 3) / 4;
}

int context_compact(SkeetCtx *ctx, size_t max_tokens) {
    if (!ctx || ctx->history_count < 4) return 0;

    size_t initial_tokens = context_estimate_tokens(ctx);
    if (max_tokens > 0 && initial_tokens <= max_tokens) {
        return 0;
    }

    size_t keep_recent = 4;
    size_t end_idx = ctx->history_count > keep_recent ? ctx->history_count - keep_recent : 1;

    for (size_t i = 1; i < end_idx; i++) {
        Message *m = &ctx->history[i];
        if (m->role.len == 4 && strncmp(m->role.ptr, "tool", 4) == 0) {
            if (m->content.len > 250) {
                char summary[128];
                snprintf(summary, sizeof(summary), "[Tool result summarized: %zu bytes compressed to save context]", m->content.len);
                free((char *)m->content.ptr);
                m->content.ptr = strdup(summary);
                m->content.len = strlen(summary);
            }
        } else if (m->role.len == 9 && strncmp(m->role.ptr, "assistant", 9) == 0) {
            if (m->content.len > 400) {
                char *compacted = malloc(350);
                if (compacted) {
                    snprintf(compacted, 350, "%.280s\n... [Assistant response summarized]", m->content.ptr);
                    free((char *)m->content.ptr);
                    m->content.ptr = compacted;
                    m->content.len = strlen(compacted);
                }
            }
        }
    }

    size_t after_tokens = context_estimate_tokens(ctx);
    return (int)(initial_tokens > after_tokens ? initial_tokens - after_tokens : 0);
}

int session_save(SkeetCtx *ctx, const char *session_name) {
    if (!ctx || !session_name || session_name[0] == '\0') return -1;

    char *sessions_dir = config_sessions_dir();
    if (!sessions_dir) return -1;
    skeet_mkdir(sessions_dir, 0755);

    char filepath[PATH_MAX];
    snprintf(filepath, sizeof(filepath), "%s/%s.json", sessions_dir, session_name);
    free(sessions_dir);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "version", 1);
    cJSON_AddStringToObject(root, "session_name", session_name);
    if (ctx->config && ctx->config->model) {
        cJSON_AddStringToObject(root, "model", ctx->config->model);
    }
    if (ctx->backend) {
        cJSON_AddStringToObject(root, "backend", ctx->backend->name);
    }

    cJSON *msgs = cJSON_CreateArray();
    for (size_t i = 0; i < ctx->history_count; i++) {
        const Message *m = &ctx->history[i];
        cJSON *item = cJSON_CreateObject();
        char *r = arena_strndup(&ctx->turn_arena, m->role.ptr, m->role.len);
        char *c = arena_strndup(&ctx->turn_arena, m->content.ptr, m->content.len);
        cJSON_AddStringToObject(item, "role", r ? r : "");
        cJSON_AddStringToObject(item, "content", c ? c : "");
        if (m->tool_call_id.len > 0) {
            char *tcid = arena_strndup(&ctx->turn_arena, m->tool_call_id.ptr, m->tool_call_id.len);
            cJSON_AddStringToObject(item, "tool_call_id", tcid ? tcid : "");
        }
        if (m->tool_calls_json.len > 0) {
            char *tcj = arena_strndup(&ctx->turn_arena, m->tool_calls_json.ptr, m->tool_calls_json.len);
            cJSON_AddStringToObject(item, "tool_calls_json", tcj ? tcj : "");
        }
        cJSON_AddItemToArray(msgs, item);
    }
    cJSON_AddItemToObject(root, "messages", msgs);

    char *json_str = cJSON_PrintBuffered(root, 4096, 1);
    cJSON_Delete(root);

    if (!json_str) return -1;

    char tmp_path[PATH_MAX];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", filepath);
    FILE *f = fopen(tmp_path, "wb");
    if (!f) {
        free(json_str);
        return -1;
    }
    fputs(json_str, f);
    fclose(f);
    free(json_str);

    remove(filepath);
    rename(tmp_path, filepath);
    return 0;
}

int session_load(SkeetCtx *ctx, const char *session_name) {
    if (!ctx || !session_name || session_name[0] == '\0') return -1;

    char *sessions_dir = config_sessions_dir();
    if (!sessions_dir) return -1;

    char filepath[PATH_MAX];
    snprintf(filepath, sizeof(filepath), "%s/%s.json", sessions_dir, session_name);
    free(sessions_dir);

    char *json_str = read_file_text(filepath, 16 * 1024 * 1024);
    if (!json_str) return -1;

    cJSON *root = cJSON_Parse(json_str);
    free(json_str);
    if (!root) return -1;

    cJSON *msgs = cJSON_GetObjectItem(root, "messages");
    if (!msgs || !cJSON_IsArray(msgs)) {
        cJSON_Delete(root);
        return -1;
    }

    /* Free current history */
    for (size_t i = 0; i < ctx->history_count; i++) {
        free((char *)ctx->history[i].role.ptr);
        free((char *)ctx->history[i].content.ptr);
        free((char *)ctx->history[i].tool_call_id.ptr);
        free((char *)ctx->history[i].tool_calls_json.ptr);
    }
    ctx->history_count = 0;

    int count = cJSON_GetArraySize(msgs);
    context_ensure_cap(ctx, count);

    for (int i = 0; i < count; i++) {
        cJSON *item = cJSON_GetArrayItem(msgs, i);
        cJSON *r = cJSON_GetObjectItem(item, "role");
        cJSON *c = cJSON_GetObjectItem(item, "content");
        cJSON *tcid = cJSON_GetObjectItem(item, "tool_call_id");
        cJSON *tcj = cJSON_GetObjectItem(item, "tool_calls_json");

        const char *r_str = (r && cJSON_IsString(r)) ? r->valuestring : "";
        const char *c_str = (c && cJSON_IsString(c)) ? c->valuestring : "";
        const char *tcid_str = (tcid && cJSON_IsString(tcid)) ? tcid->valuestring : "";
        const char *tcj_str = (tcj && cJSON_IsString(tcj)) ? tcj->valuestring : "";

        ctx->history[ctx->history_count++] = (Message){
            .role = (Str){ .ptr = strdup(r_str), .len = strlen(r_str) },
            .content = (Str){ .ptr = strdup(c_str), .len = strlen(c_str) },
            .tool_call_id = (Str){ .ptr = strdup(tcid_str), .len = strlen(tcid_str) },
            .tool_calls_json = (Str){ .ptr = strdup(tcj_str), .len = strlen(tcj_str) }
        };
    }

    cJSON_Delete(root);
    return 0;
}

int session_list(SkeetCtx *ctx, char ***out_names, size_t *out_count) {
    (void)ctx;
    if (!out_names || !out_count) return -1;
    *out_names = NULL;
    *out_count = 0;

    char *sessions_dir = config_sessions_dir();
    if (!sessions_dir) return -1;

    size_t cap = 16;
    char **names = malloc(cap * sizeof(char *));
    size_t count = 0;

#if defined(SKEET_OS_WINDOWS)
    char pattern[PATH_MAX];
    snprintf(pattern, sizeof(pattern), "%s\\*.json", sessions_dir);
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(pattern, &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                char sname[256];
                snprintf(sname, sizeof(sname), "%s", fd.cFileName);
                char *dot = strrchr(sname, '.');
                if (dot) *dot = '\0';
                if (count >= cap) {
                    cap *= 2;
                    names = realloc(names, cap * sizeof(char *));
                }
                names[count++] = strdup(sname);
            }
        } while (FindNextFileA(hFind, &fd));
        FindClose(hFind);
    }
#else
    DIR *d = opendir(sessions_dir);
    if (d) {
        struct dirent *de;
        while ((de = readdir(d)) != NULL) {
            size_t l = strlen(de->d_name);
            if (l > 5 && strcmp(de->d_name + l - 5, ".json") == 0) {
                char sname[256];
                snprintf(sname, sizeof(sname), "%s", de->d_name);
                sname[l - 5] = '\0';
                if (count >= cap) {
                    cap *= 2;
                    names = realloc(names, cap * sizeof(char *));
                }
                names[count++] = strdup(sname);
            }
        }
        closedir(d);
    }
#endif

    free(sessions_dir);
    *out_names = names;
    *out_count = count;
    return 0;
}

void session_list_free(char **names, size_t count) {
    if (!names) return;
    for (size_t i = 0; i < count; i++) {
        free(names[i]);
    }
    free(names);
}
