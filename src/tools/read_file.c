#include "tools/builtins.h"
#include "tools/registry.h"
#include "context.h"
#include "compat.h"
#include "cJSON.h"
#include "json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define READ_FILE_MAX_BYTES (64 * 1024)
#define READ_FILE_DEFAULT_LIMIT 2000

static int is_binary_buffer(const unsigned char *buf, size_t len) {
    for (size_t i = 0; i < len; i++) {
        if (buf[i] == 0) {
            return 1;
        }
    }
    return 0;
}

static int run_read_file(SkeetCtx *ctx, const ToolCall *call, ToolResult *result, void *userdata) {
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
    if (!rel_path) {
        cJSON_Delete(root);
        result->content = str_from_cstr("Error: Missing required argument 'path'");
        return 0;
    }

    int offset = 1;
    const cJSON *off_item = json_obj(root, "offset");
    if (off_item && cJSON_IsNumber(off_item) && off_item->valueint > 1) {
        offset = off_item->valueint;
    }

    int limit = READ_FILE_DEFAULT_LIMIT;
    const cJSON *lim_item = json_obj(root, "limit");
    if (lim_item && cJSON_IsNumber(lim_item) && lim_item->valueint > 0) {
        limit = lim_item->valueint;
    }

    char *abs_path = skeet_resolve_path(ctx->workspace_root, rel_path);
    if (!abs_path) {
        cJSON_Delete(root);
        result->content = str_from_cstr("Error: Path is outside workspace or invalid");
        return 0;
    }

    FILE *f = fopen(abs_path, "rb");
    if (!f) {
        free(abs_path);
        cJSON_Delete(root);
        result->content = str_from_cstr("Error: File not found or could not be opened");
        return 0;
    }

    unsigned char check_buf[1024];
    size_t check_len = fread(check_buf, 1, sizeof(check_buf), f);
    if (check_len > 0 && is_binary_buffer(check_buf, check_len)) {
        fclose(f);
        free(abs_path);
        cJSON_Delete(root);
        result->content = str_from_cstr("Error: File appears to be binary");
        return 0;
    }
    fseek(f, 0, SEEK_SET);

    StrBuf out;
    strbuf_init(&out);

    char line_buf[4096];
    int cur_line = 1;
    int lines_read = 0;
    size_t bytes_read = 0;

    while (fgets(line_buf, sizeof(line_buf), f)) {
        if (cur_line >= offset && lines_read < limit) {
            char prefix[32];
            snprintf(prefix, sizeof(prefix), "%6d | ", cur_line);
            strbuf_puts(&out, prefix);
            strbuf_puts(&out, line_buf);
            lines_read++;
            bytes_read += strlen(line_buf);

            if (bytes_read >= READ_FILE_MAX_BYTES) {
                strbuf_puts(&out, "\n[Output truncated: reached 64KB limit]\n");
                break;
            }
        }
        cur_line++;
    }

    fclose(f);
    skeet_mark_file_read(ctx, rel_path);
    free(abs_path);
    cJSON_Delete(root);

    result->is_error = 0;
    char *detached = strbuf_detach(&out);
    if (!detached) {
        result->content = str_from_cstr("");
        return 0;
    }
    size_t dlen = strlen(detached);
    char *arena_text = arena_strndup(&ctx->turn_arena, detached, dlen);
    free(detached);
    result->content = (Str){ .ptr = arena_text, .len = dlen };
    return 0;
}

int tool_register_read_file(SkeetCtx *ctx) {
    ToolDef def = {
        .size = sizeof(ToolDef),
        .name = "read_file",
        .description = "Read file contents with line numbers and truncation limits",
        .schema_json = "{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\",\"description\":\"Path to file relative to workspace\"},\"offset\":{\"type\":\"integer\",\"description\":\"1-based line offset to start reading from\"},\"limit\":{\"type\":\"integer\",\"description\":\"Maximum number of lines to read\"}},\"required\":[\"path\"]}",
        .flags = TOOL_READONLY,
        .run = run_read_file,
        .userdata = NULL,
        .owner = "core"
    };
    return tool_register(ctx, &def);
}
