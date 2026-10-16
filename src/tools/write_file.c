#include "tools/builtins.h"
#include "tools/registry.h"
#include "context.h"
#include "compat.h"
#include "cJSON.h"
#include "json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int make_parent_dirs(const char *path) {
    char temp[PATH_MAX];
    strncpy(temp, path, sizeof(temp) - 1);
    temp[sizeof(temp) - 1] = '\0';

    char *slash = strrchr(temp, '/');
#if defined(SKEET_OS_WINDOWS)
    char *bslash = strrchr(temp, '\\');
    if (bslash && (!slash || bslash > slash)) {
        slash = bslash;
    }
#endif
    if (!slash || slash == temp) {
        return 0;
    }
    *slash = '\0';

    char *p = temp;
    if (*p == '/') {
        p++;
    }
    while (*p) {
        if (*p == '/'
#if defined(SKEET_OS_WINDOWS)
            || *p == '\\'
#endif
        ) {
            char save = *p;
            *p = '\0';
            skeet_mkdir(temp, 0755);
            *p = save;
        }
        p++;
    }
    skeet_mkdir(temp, 0755);
    return 0;
}

static int atomic_write(const char *target_path, const char *data, size_t len) {
    make_parent_dirs(target_path);

    char tmp_path[PATH_MAX];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp.%d", target_path, rand());

    FILE *f = fopen(tmp_path, "wb");
    if (!f) {
        return -1;
    }
    if (len > 0) {
        if (fwrite(data, 1, len, f) != len) {
            fclose(f);
            remove(tmp_path);
            return -1;
        }
    }
    fclose(f);

    if (rename(tmp_path, target_path) != 0) {
        remove(tmp_path);
        return -1;
    }
    return 0;
}

static int run_write_file(SkeetCtx *ctx, const ToolCall *call, ToolResult *result, void *userdata) {
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

    const char *rel_path = json_obj_str(root, "path");
    const char *content = json_obj_str(root, "content");

    if (!rel_path || !content) {
        cJSON_Delete(root);
        result->content = str_from_cstr("Error: Missing required arguments (path, content)");
        return 0;
    }

    char *abs_path = skeet_resolve_path(ctx->workspace_root, rel_path);
    if (!abs_path) {
        cJSON_Delete(root);
        result->content = str_from_cstr("Error: Invalid path or outside workspace");
        return 0;
    }

    struct stat st;
    if (stat(abs_path, &st) == 0 && S_ISREG(st.st_mode)) {
        if (!skeet_is_file_read(ctx, rel_path)) {
            char err_msg[512];
            snprintf(err_msg, sizeof(err_msg), "Error: existing file '%s' must be read with read_file before overwriting", rel_path);
            free(abs_path);
            cJSON_Delete(root);
            char *arena_err = arena_strndup(&ctx->turn_arena, err_msg, strlen(err_msg));
            result->content = (Str){ .ptr = arena_err, .len = strlen(err_msg) };
            return 0;
        }
    }

    size_t clen = strlen(content);
    if (atomic_write(abs_path, content, clen) != 0) {
        free(abs_path);
        cJSON_Delete(root);
        result->content = str_from_cstr("Error: Failed to write file");
        return 0;
    }

    skeet_mark_file_read(ctx, rel_path);
    free(abs_path);

    result->is_error = 0;
    char msg[512];
    snprintf(msg, sizeof(msg), "Successfully wrote %zu bytes to '%s'.", clen, rel_path);
    cJSON_Delete(root);

    size_t mlen = strlen(msg);
    char *arena_msg = arena_strndup(&ctx->turn_arena, msg, mlen);
    result->content = (Str){ .ptr = arena_msg, .len = mlen };
    return 0;
}

int tool_register_write_file(SkeetCtx *ctx) {
    ToolDef def = {
        .size = sizeof(ToolDef),
        .name = "write_file",
        .description = "Write entire contents to a file atomically (creates parent directories)",
        .schema_json = "{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\",\"description\":\"Path to file relative to workspace\"},\"content\":{\"type\":\"string\",\"description\":\"Full text content to write\"}},\"required\":[\"path\",\"content\"]}",
        .flags = TOOL_NEEDS_CONFIRM,
        .run = run_write_file,
        .userdata = NULL,
        .owner = "core"
    };
    return tool_register(ctx, &def);
}
