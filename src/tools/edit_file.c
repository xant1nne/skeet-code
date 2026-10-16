#include "tools/builtins.h"
#include "tools/registry.h"
#include "tools/diff.h"
#include "context.h"
#include "compat.h"
#include "cJSON.h"
#include "json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *read_entire_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) {
        fclose(f);
        return NULL;
    }

    char *buf = malloc(sz + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t r = fread(buf, 1, sz, f);
    buf[r] = '\0';
    fclose(f);
    if (out_len) {
        *out_len = r;
    }
    return buf;
}

static int atomic_write_file(const char *target_path, const char *data, size_t len) {
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

static int run_edit_file(SkeetCtx *ctx, const ToolCall *call, ToolResult *result, void *userdata) {
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
    const char *old_str = json_obj_str(root, "old_string");
    const char *new_str = json_obj_str(root, "new_string");
    int replace_all = 0;
    const cJSON *ra_item = json_obj(root, "replace_all");
    if (ra_item && cJSON_IsTrue(ra_item)) {
        replace_all = 1;
    }

    if (!rel_path || !old_str || !new_str) {
        cJSON_Delete(root);
        result->content = str_from_cstr("Error: Missing required arguments (path, old_string, new_string)");
        return 0;
    }

    if (!skeet_is_file_read(ctx, rel_path)) {
        char err_msg[512];
        snprintf(err_msg, sizeof(err_msg), "Error: file '%s' must be read with read_file before editing", rel_path);
        cJSON_Delete(root);
        char *arena_err = arena_strndup(&ctx->turn_arena, err_msg, strlen(err_msg));
        result->content = (Str){ .ptr = arena_err, .len = strlen(err_msg) };
        return 0;
    }

    char *abs_path = skeet_resolve_path(ctx->workspace_root, rel_path);
    if (!abs_path) {
        cJSON_Delete(root);
        result->content = str_from_cstr("Error: Invalid path or outside workspace");
        return 0;
    }

    size_t file_len = 0;
    char *content = read_entire_file(abs_path, &file_len);
    if (!content) {
        free(abs_path);
        cJSON_Delete(root);
        result->content = str_from_cstr("Error: File not found or could not be read");
        return 0;
    }

    size_t old_len = strlen(old_str);
    if (old_len == 0) {
        free(content);
        free(abs_path);
        cJSON_Delete(root);
        result->content = str_from_cstr("Error: old_string cannot be empty");
        return 0;
    }

    size_t match_count = 0;
    const char *pos = content;
    while ((pos = strstr(pos, old_str)) != NULL) {
        match_count++;
        pos += old_len;
    }

    if (match_count == 0) {
        free(content);
        free(abs_path);
        cJSON_Delete(root);
        result->content = str_from_cstr("Error: old_string not found in file");
        return 0;
    }

    if (match_count > 1 && !replace_all) {
        free(content);
        free(abs_path);
        cJSON_Delete(root);
        result->content = str_from_cstr("Error: old_string occurs multiple times in file; please use replace_all or provide more unique context");
        return 0;
    }

    StrBuf new_content;
    strbuf_init(&new_content);

    pos = content;
    size_t replaced = 0;
    while (*pos) {
        const char *match = strstr(pos, old_str);
        if (!match || (!replace_all && replaced > 0)) {
            strbuf_puts(&new_content, pos);
            break;
        }
        strbuf_append(&new_content, pos, match - pos);
        strbuf_puts(&new_content, new_str);
        pos = match + old_len;
        replaced++;
    }

    char *final_str = strbuf_detach(&new_content);
    if (atomic_write_file(abs_path, final_str, strlen(final_str)) != 0) {
        free(final_str);
        free(content);
        free(abs_path);
        cJSON_Delete(root);
        result->content = str_from_cstr("Error: Failed to write file");
        return 0;
    }

    char *diff_preview = diff_generate_preview(rel_path, old_str, new_str);
    char success_msg[4096];
    snprintf(success_msg, sizeof(success_msg), "File '%s' successfully edited.\n%s", rel_path, diff_preview ? diff_preview : "");
    free(diff_preview);
    free(final_str);
    free(content);
    free(abs_path);
    cJSON_Delete(root);

    result->is_error = 0;
    size_t slen = strlen(success_msg);
    char *arena_res = arena_strndup(&ctx->turn_arena, success_msg, slen);
    result->content = (Str){ .ptr = arena_res, .len = slen };
    return 0;
}

int tool_register_edit_file(SkeetCtx *ctx) {
    ToolDef def = {
        .size = sizeof(ToolDef),
        .name = "edit_file",
        .description = "Perform exact string replacement in a previously read file",
        .schema_json = "{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\",\"description\":\"Path to file relative to workspace\"},\"old_string\":{\"type\":\"string\",\"description\":\"Exact text to replace\"},\"new_string\":{\"type\":\"string\",\"description\":\"Replacement text\"},\"replace_all\":{\"type\":\"boolean\",\"description\":\"Replace all occurrences (default false)\"}},\"required\":[\"path\",\"old_string\",\"new_string\"]}",
        .flags = TOOL_NEEDS_CONFIRM,
        .run = run_edit_file,
        .userdata = NULL,
        .owner = "core"
    };
    return tool_register(ctx, &def);
}
