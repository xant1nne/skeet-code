#ifndef SKEET_TUI_VIEW_H
#define SKEET_TUI_VIEW_H

#include "term.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ViewLineType {
    LINE_NORMAL = 0,
    LINE_USER_HEADER,
    LINE_USER_TEXT,
    LINE_ASSISTANT_HEADER,
    LINE_ASSISTANT_TEXT,
    LINE_TOOL_START,
    LINE_TOOL_CONTENT,
    LINE_TOOL_END,
    LINE_DIFF_ADD,
    LINE_DIFF_DEL,
    LINE_DIFF_HDR,
    LINE_INFO,
    LINE_ERROR
} ViewLineType;

typedef struct ViewLine {
    ViewLineType type;
    char *text;
} ViewLine;

typedef struct ViewStatusBar {
    char model[64];
    char backend[32];
    size_t tokens_used;
    size_t tokens_max;
    double tok_per_sec;
    char status[64];
    int spinner_idx;
} ViewStatusBar;

typedef struct View {
    ViewLine *lines;
    size_t line_count;
    size_t line_cap;
    int scroll_offset; /* 0 = bottom, >0 = scrolled up */
    ViewStatusBar status;
} View;

View *view_new(void);
void view_free(View *view);

void view_clear(View *view);
void view_append_line(View *view, ViewLineType type, const char *text);
void view_append_text(View *view, ViewLineType type, const char *multiline_text);
void view_append_tool_box(View *view, const char *tool_name, const char *args, const char *result, int is_error);
void view_append_diff(View *view, const char *diff_text);

void view_scroll_up(View *view, int lines);
void view_scroll_down(View *view, int lines);
void view_scroll_page_up(View *view, int page_height);
void view_scroll_page_down(View *view, int page_height);
void view_scroll_bottom(View *view);

void view_set_status(View *view, const char *model, const char *backend, size_t tokens, size_t max_tokens, double tps, const char *status);
void view_step_spinner(View *view);

/* Renders visible viewport and status bar. Content rows = term_rows - prompt_height */
void view_render(View *view, TermSize size, int prompt_height);

#ifdef __cplusplus
}
#endif

#endif /* SKEET_TUI_VIEW_H */

