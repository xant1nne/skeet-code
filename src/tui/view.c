#include "view.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *SPINNER_FRAMES[] = {
    "⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"
};
static const int SPINNER_COUNT = 10;

View *view_new(void) {
    View *v = calloc(1, sizeof(View));
    if (!v) return NULL;
    v->line_cap = 128;
    v->lines = malloc(v->line_cap * sizeof(ViewLine));
    v->line_count = 0;
    v->scroll_offset = 0;
    snprintf(v->status.status, sizeof(v->status.status), "idle");
    return v;
}

void view_free(View *view) {
    if (!view) return;
    for (size_t i = 0; i < view->line_count; i++) {
        free(view->lines[i].text);
    }
    free(view->lines);
    free(view);
}

void view_clear(View *view) {
    if (!view) return;
    for (size_t i = 0; i < view->line_count; i++) {
        free(view->lines[i].text);
    }
    view->line_count = 0;
    view->scroll_offset = 0;
}

void view_append_line(View *view, ViewLineType type, const char *text) {
    if (!view) return;
    if (view->line_count >= view->line_cap) {
        size_t new_cap = view->line_cap * 2;
        ViewLine *nl = realloc(view->lines, new_cap * sizeof(ViewLine));
        if (!nl) return;
        view->lines = nl;
        view->line_cap = new_cap;
    }
    view->lines[view->line_count].type = type;
    view->lines[view->line_count].text = text ? strdup(text) : strdup("");
    view->line_count++;
}

void view_append_text(View *view, ViewLineType type, const char *multiline_text) {
    if (!view || !multiline_text) return;
    const char *p = multiline_text;
    while (*p) {
        const char *nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : strlen(p);
        if (len > 0 && p[len - 1] == '\r') len--;
        char *line = malloc(len + 1);
        if (line) {
            memcpy(line, p, len);
            line[len] = '\0';
            view_append_line(view, type, line);
            free(line);
        }
        if (!nl) break;
        p = nl + 1;
    }
}

void view_append_tool_box(View *view, const char *tool_name, const char *args, const char *result, int is_error) {
    if (!view) return;
    char header[256];
    snprintf(header, sizeof(header), "╭── Tool: %s %s", tool_name ? tool_name : "unknown", is_error ? "[FAILED]" : "[OK]");
    view_append_line(view, LINE_TOOL_START, header);

    if (args && args[0]) {
        char args_hdr[256];
        snprintf(args_hdr, sizeof(args_hdr), "│ args: %s", args);
        view_append_line(view, LINE_TOOL_CONTENT, args_hdr);
    }

    if (result && result[0]) {
        const char *p = result;
        while (*p) {
            const char *nl = strchr(p, '\n');
            size_t len = nl ? (size_t)(nl - p) : strlen(p);
            if (len > 0 && p[len - 1] == '\r') len--;
            char buf[512];
            snprintf(buf, sizeof(buf), "│ %.*s", (int)(len < sizeof(buf) - 5 ? len : sizeof(buf) - 5), p);
            view_append_line(view, LINE_TOOL_CONTENT, buf);
            if (!nl) break;
            p = nl + 1;
        }
    }

    view_append_line(view, LINE_TOOL_END, "╰──────────────────────────────────────────");
}

void view_append_diff(View *view, const char *diff_text) {
    if (!view || !diff_text) return;
    const char *p = diff_text;
    while (*p) {
        const char *nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : strlen(p);
        if (len > 0 && p[len - 1] == '\r') len--;

        char *line = malloc(len + 1);
        if (line) {
            memcpy(line, p, len);
            line[len] = '\0';
            ViewLineType type = LINE_NORMAL;
            if (line[0] == '+') type = LINE_DIFF_ADD;
            else if (line[0] == '-') type = LINE_DIFF_DEL;
            else if (line[0] == '@' && line[1] == '@') type = LINE_DIFF_HDR;
            view_append_line(view, type, line);
            free(line);
        }
        if (!nl) break;
        p = nl + 1;
    }
}

void view_scroll_up(View *view, int lines) {
    if (!view) return;
    view->scroll_offset += lines;
    if (view->scroll_offset > (int)view->line_count) {
        view->scroll_offset = (int)view->line_count;
    }
}

void view_scroll_down(View *view, int lines) {
    if (!view) return;
    view->scroll_offset -= lines;
    if (view->scroll_offset < 0) {
        view->scroll_offset = 0;
    }
}

void view_scroll_page_up(View *view, int page_height) {
    view_scroll_up(view, page_height > 1 ? page_height - 1 : 1);
}

void view_scroll_page_down(View *view, int page_height) {
    view_scroll_down(view, page_height > 1 ? page_height - 1 : 1);
}

void view_scroll_bottom(View *view) {
    if (view) view->scroll_offset = 0;
}

void view_set_status(View *view, const char *model, const char *backend, size_t tokens, size_t max_tokens, double tps, const char *status) {
    if (!view) return;
    if (model) snprintf(view->status.model, sizeof(view->status.model), "%s", model);
    if (backend) snprintf(view->status.backend, sizeof(view->status.backend), "%s", backend);
    view->status.tokens_used = tokens;
    view->status.tokens_max = max_tokens;
    view->status.tok_per_sec = tps;
    if (status) snprintf(view->status.status, sizeof(view->status.status), "%s", status);
}

void view_step_spinner(View *view) {
    if (!view) return;
    view->status.spinner_idx = (view->status.spinner_idx + 1) % SPINNER_COUNT;
}

void view_render(View *view, TermSize size, int prompt_height) {
    if (!view) return;

    /* Content area: from row 1 to rows - 1 - prompt_height */
    int content_rows = size.rows - 1 - prompt_height;
    if (content_rows < 1) content_rows = 1;

    int total = (int)view->line_count;
    int end_idx = total - view->scroll_offset;
    if (end_idx > total) end_idx = total;
    int start_idx = end_idx - content_rows;
    if (start_idx < 0) start_idx = 0;

    /* Draw viewport */
    for (int r = 0; r < content_rows; r++) {
        term_cursor_move(r + 1, 1);
        term_clear_line();

        int line_idx = start_idx + r;
        if (line_idx < end_idx && line_idx >= 0 && line_idx < total) {
            ViewLine *vl = &view->lines[line_idx];
            switch (vl->type) {
            case LINE_USER_HEADER:
                printf("\033[1;32m%s\033[0m", vl->text);
                break;
            case LINE_ASSISTANT_HEADER:
                printf("\033[1;36m%s\033[0m", vl->text);
                break;
            case LINE_TOOL_START:
                printf("\033[1;34m%s\033[0m", vl->text);
                break;
            case LINE_TOOL_CONTENT:
                printf("\033[38;5;248m%s\033[0m", vl->text);
                break;
            case LINE_TOOL_END:
                printf("\033[1;34m%s\033[0m", vl->text);
                break;
            case LINE_DIFF_ADD:
                printf("\033[32m%s\033[0m", vl->text);
                break;
            case LINE_DIFF_DEL:
                printf("\033[31m%s\033[0m", vl->text);
                break;
            case LINE_DIFF_HDR:
                printf("\033[35;1m%s\033[0m", vl->text);
                break;
            case LINE_ERROR:
                printf("\033[1;31m%s\033[0m", vl->text);
                break;
            case LINE_INFO:
                printf("\033[33m%s\033[0m", vl->text);
                break;
            default:
                printf("%s", vl->text);
                break;
            }
        }
    }

    /* Draw Status Bar at row (size.rows - prompt_height) */
    int status_row = size.rows - prompt_height;
    term_cursor_move(status_row, 1);
    term_clear_line();

    const char *spinner = SPINNER_FRAMES[view->status.spinner_idx % SPINNER_COUNT];
    char left_buf[128];
    snprintf(left_buf, sizeof(left_buf), " SKEET │ %s │ %s │ %s %s ",
             view->status.backend[0] ? view->status.backend : "none",
             view->status.model[0] ? view->status.model : "no model",
             spinner,
             view->status.status);

    char right_buf[128];
    if (view->status.tokens_max > 0) {
        double pct = (double)view->status.tokens_used * 100.0 / (double)view->status.tokens_max;
        snprintf(right_buf, sizeof(right_buf), " %zu/%zu tok (%.0f%%) │ %.1f t/s ",
                 view->status.tokens_used, view->status.tokens_max, pct, view->status.tok_per_sec);
    } else if (view->status.tokens_used > 0) {
        snprintf(right_buf, sizeof(right_buf), " %zu tok │ %.1f t/s ",
                 view->status.tokens_used, view->status.tok_per_sec);
    } else {
        snprintf(right_buf, sizeof(right_buf), " Ready ");
    }

    int left_len = (int)strlen(left_buf);
    int right_len = (int)strlen(right_buf);
    int pad = size.cols - left_len - right_len;
    if (pad < 1) pad = 1;

    printf("\033[7m%s", left_buf);
    for (int i = 0; i < pad; i++) putchar(' ');
    printf("%s\033[0m", right_buf);

    fflush(stdout);
}

