#include "tools/builtins.h"
#include "tools/registry.h"
#include "context.h"
#include "compat.h"
#include "cJSON.h"
#include "json.h"

#include <dirent.h>
#include <fnmatch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define GLOB_MAX_MATCHES 200

static int should_ignore(const char *name) {
    return strcmp(name, ".") == 0 ||
           strcmp(name, "..") == 0 ||
           strcmp(name, ".git") == 0 ||
           strcmp(name, "build") == 0 ||
           strcmp(name, "node_modules") == 0 ||
           strcmp(name, ".cache") == 0;
}

static void search_dir(const char *workspace_root, const char *rel_base, const char *pattern, StrBuf *out, size_t *count, int *hit_limit) {
    if (*hit_limit || *count >= GLOB_MAX_MATCHES) {
        *hit_limit = 1;
        return;
    }

    char full_dir[PATH_MAX];
    if (rel_base && rel_base[0] && strcmp(rel_base, ".") != 0) {
        snprintf(full_dir, sizeof(full_dir), "%s/%s", workspace_root, rel_base);
    } else {
        snprintf(full_dir, sizeof(full_dir), "%s", workspace_root);
    }

    DIR *d = opendir(full_dir);
    if (!d) {
        return;
    }

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (should_ignore(ent->d_name)) {
            continue;
        }

        char child_rel[PATH_MAX];
        if (rel_base && rel_base[0] && strcmp(rel_base, ".") != 0) {
            snprintf(child_rel, sizeof(child_rel), "%s/%s", rel_base, ent->d_name);
        } else {
            snprintf(child_rel, sizeof(child_rel), "%s", ent->d_name);
        }

        char child_full[PATH_MAX * 2];
        snprintf(child_full, sizeof(child_full), "%s/%s", workspace_root, child_rel);

        struct stat st;
        if (stat(child_full, &st) != 0) {
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            search_dir(workspace_root, child_rel, pattern, out, count, hit_limit);
            if (*hit_limit) {
                break;
            }
        } else if (S_ISREG(st.st_mode)) {
            int match = 0;
            if (fnmatch(pattern, child_rel, FNM_PATHNAME) == 0 ||
                fnmatch(pattern, ent->d_name, 0) == 0) {
                match = 1;
            }

            if (match) {
                if (*count >= GLOB_MAX_MATCHES) {
                    *hit_limit = 1;
                    break;
                }
                strbuf_puts(out, child_rel);
                strbuf_puts(out, "\n");
                (*count)++;
            }
        }
    }

    closedir(d);
}

static int run_glob(SkeetCtx *ctx, const ToolCall *call, ToolResult *result, void *userdata) {
    (void)userdata;
    result->is_error = 1;

    char *args_str = arena_strndup(&ctx->turn_arena, call->args_json.ptr, call->args_json.len);
    if (!args_str) {
        result->content = str_from_cstr("Error: Out of memory");
        return 0;
    }

    cJSON *root = cJSON_Parse(args_str);
    if (!root) {
        result->content = str_from_cstr("Error: Invalid JSON arguments");
        return 0;
    }

    const char *pattern = json_obj_str(root, "pattern");
    if (!pattern) {
        cJSON_Delete(root);
        result->content = str_from_cstr("Error: Missing required argument 'pattern'");
        return 0;
    }

    const char *search_path = json_obj_str(root, "path");
    if (!search_path) {
        search_path = ".";
    }

    char *abs_search = skeet_resolve_path(ctx->workspace_root, search_path);
    if (!abs_search) {
        cJSON_Delete(root);
        result->content = str_from_cstr("Error: Invalid search path");
        return 0;
    }
    free(abs_search);

    StrBuf out;
    strbuf_init(&out);
    size_t count = 0;
    int hit_limit = 0;

    search_dir(ctx->workspace_root, strcmp(search_path, ".") == 0 ? "" : search_path, pattern, &out, &count, &hit_limit);

    cJSON_Delete(root);
    result->is_error = 0;

    if (count == 0) {
        result->content = str_from_cstr("No files matched pattern.");
        strbuf_free(&out);
        return 0;
    }

    if (hit_limit) {
        strbuf_puts(&out, "[Output truncated: reached 200 matches limit]\n");
    }

    char *detached = strbuf_detach(&out);
    size_t dlen = strlen(detached);
    char *arena_text = arena_strndup(&ctx->turn_arena, detached, dlen);
    free(detached);
    result->content = (Str){ .ptr = arena_text, .len = dlen };
    return 0;
}

int tool_register_glob(SkeetCtx *ctx) {
    ToolDef def = {
        .size = sizeof(ToolDef),
        .name = "glob",
        .description = "Find files matching pattern recursively (ignores .git, build, node_modules)",
        .schema_json = "{\"type\":\"object\",\"properties\":{\"pattern\":{\"type\":\"string\",\"description\":\"Glob pattern like '*.c' or 'src/**/*.h'\"},\"path\":{\"type\":\"string\",\"description\":\"Optional directory to search within (defaults to '.')\"}},\"required\":[\"pattern\"]}",
        .flags = TOOL_READONLY,
        .run = run_glob,
        .userdata = NULL,
        .owner = "core"
    };
    return tool_register(ctx, &def);
}
