#include "tools/builtins.h"
#include "tools/registry.h"
#include "context.h"
#include "compat.h"
#include "cJSON.h"
#include "json.h"

#include <dirent.h>
#include <fnmatch.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define GREP_MAX_MATCHES 200

static int should_ignore(const char *name) {
    return strcmp(name, ".") == 0 ||
           strcmp(name, "..") == 0 ||
           strcmp(name, ".git") == 0 ||
           strcmp(name, "build") == 0 ||
           strcmp(name, "node_modules") == 0 ||
           strcmp(name, ".cache") == 0;
}

static void grep_file(const char *full_path, const char *rel_path, regex_t *re, StrBuf *out, size_t *count, int *hit_limit) {
    if (*hit_limit || *count >= GREP_MAX_MATCHES) {
        *hit_limit = 1;
        return;
    }

    FILE *f = fopen(full_path, "r");
    if (!f) {
        return;
    }

    char line[4096];
    int line_num = 1;

    while (fgets(line, sizeof(line), f)) {
        size_t llen = strlen(line);
        if (llen > 0 && (line[llen - 1] == '\n' || line[llen - 1] == '\r')) {
            line[--llen] = '\0';
        }

        if (regexec(re, line, 0, NULL, 0) == 0) {
            char entry[5120];
            snprintf(entry, sizeof(entry), "%s:%d:%s\n", rel_path, line_num, line);
            strbuf_puts(out, entry);
            (*count)++;

            if (*count >= GREP_MAX_MATCHES) {
                *hit_limit = 1;
                break;
            }
        }
        line_num++;
    }

    fclose(f);
}

static void grep_dir(const char *workspace_root, const char *rel_base, const char *file_filter, regex_t *re, StrBuf *out, size_t *count, int *hit_limit) {
    if (*hit_limit || *count >= GREP_MAX_MATCHES) {
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
            grep_dir(workspace_root, child_rel, file_filter, re, out, count, hit_limit);
            if (*hit_limit) {
                break;
            }
        } else if (S_ISREG(st.st_mode)) {
            if (file_filter && file_filter[0]) {
                if (fnmatch(file_filter, ent->d_name, 0) != 0 &&
                    fnmatch(file_filter, child_rel, FNM_PATHNAME) != 0) {
                    continue;
                }
            }
            grep_file(child_full, child_rel, re, out, count, hit_limit);
            if (*hit_limit) {
                break;
            }
        }
    }

    closedir(d);
}

static int run_grep(SkeetCtx *ctx, const ToolCall *call, ToolResult *result, void *userdata) {
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

    const char *glob_filter = json_obj_str(root, "glob");

    int ignore_case = 0;
    const cJSON *ic_item = json_obj(root, "ignore_case");
    if (ic_item && cJSON_IsTrue(ic_item)) {
        ignore_case = 1;
    }

    int cflags = REG_EXTENDED;
    if (ignore_case) {
        cflags |= REG_ICASE;
    }

    regex_t re;
    if (regcomp(&re, pattern, cflags) != 0) {
        cJSON_Delete(root);
        result->content = str_from_cstr("Error: Invalid regular expression");
        return 0;
    }

    StrBuf out;
    strbuf_init(&out);
    size_t count = 0;
    int hit_limit = 0;

    char *abs_search = skeet_resolve_path(ctx->workspace_root, search_path);
    if (abs_search) {
        struct stat st;
        if (stat(abs_search, &st) == 0 && S_ISREG(st.st_mode)) {
            grep_file(abs_search, search_path, &re, &out, &count, &hit_limit);
        } else {
            grep_dir(ctx->workspace_root, strcmp(search_path, ".") == 0 ? "" : search_path, glob_filter, &re, &out, &count, &hit_limit);
        }
        free(abs_search);
    } else {
        grep_dir(ctx->workspace_root, strcmp(search_path, ".") == 0 ? "" : search_path, glob_filter, &re, &out, &count, &hit_limit);
    }

    regfree(&re);
    cJSON_Delete(root);

    result->is_error = 0;
    if (count == 0) {
        result->content = str_from_cstr("No matches found.");
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

int tool_register_grep(SkeetCtx *ctx) {
    ToolDef def = {
        .size = sizeof(ToolDef),
        .name = "grep",
        .description = "Search files for a POSIX regex pattern (outputs file:line:text)",
        .schema_json = "{\"type\":\"object\",\"properties\":{\"pattern\":{\"type\":\"string\",\"description\":\"Regular expression pattern to match\"},\"path\":{\"type\":\"string\",\"description\":\"Path to search in (defaults to '.')\"},\"glob\":{\"type\":\"string\",\"description\":\"Optional file filter glob (e.g. '*.c')\"},\"ignore_case\":{\"type\":\"boolean\",\"description\":\"Case insensitive search\"},\"context\":{\"type\":\"integer\",\"description\":\"Number of context lines\"}},\"required\":[\"pattern\"]}",
        .flags = TOOL_READONLY,
        .run = run_grep,
        .userdata = NULL,
        .owner = "core"
    };
    return tool_register(ctx, &def);
}
