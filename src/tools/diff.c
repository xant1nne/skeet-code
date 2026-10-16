#include "tools/diff.h"
#include "str.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *diff_generate_preview(const char *rel_path, const char *old_content, const char *new_content) {
    StrBuf buf;
    strbuf_init(&buf);

    char header[512];
    snprintf(header, sizeof(header), "\033[1m--- a/%s\n+++ b/%s\033[0m\n",
             rel_path ? rel_path : "file", rel_path ? rel_path : "file");
    strbuf_puts(&buf, header);

    if (old_content && old_content[0]) {
        const char *cur = old_content;
        while (*cur) {
            const char *next = strchr(cur, '\n');
            size_t len = next ? (size_t)(next - cur) : strlen(cur);
            strbuf_puts(&buf, "\033[31m- ");
            strbuf_append(&buf, cur, len);
            strbuf_puts(&buf, "\033[0m\n");
            if (!next) {
                break;
            }
            cur = next + 1;
        }
    }

    if (new_content && new_content[0]) {
        const char *cur = new_content;
        while (*cur) {
            const char *next = strchr(cur, '\n');
            size_t len = next ? (size_t)(next - cur) : strlen(cur);
            strbuf_puts(&buf, "\033[32m+ ");
            strbuf_append(&buf, cur, len);
            strbuf_puts(&buf, "\033[0m\n");
            if (!next) {
                break;
            }
            cur = next + 1;
        }
    }

    return strbuf_detach(&buf);
}

