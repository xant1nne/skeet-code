#ifndef SKEET_TUI_H
#define SKEET_TUI_H

#include "term.h"
#include "view.h"
#include "input.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Tui {
    View *view;
    InputEditor *input;
    int is_interactive;
    int alt_screen_enabled;
    char *stream_accum;
    size_t stream_accum_len;
    size_t stream_accum_cap;
} Tui;

Tui *tui_new(const char *history_path);
void tui_free(Tui *tui);

int tui_start(Tui *tui);
void tui_stop(Tui *tui);

void tui_redraw(Tui *tui);

/* Prompt reading loop. Returns 0 on success, -1 on Ctrl+C / EOF / exit */
int tui_read_line(Tui *tui, const char *prompt, char *out_buf, size_t max_len);

void tui_append_user_message(Tui *tui, const char *text);
void tui_start_assistant_message(Tui *tui);
void tui_stream_token(Tui *tui, const char *token);
void tui_end_assistant_message(Tui *tui);

void tui_append_tool(Tui *tui, const char *tool_name, const char *args, const char *result, int is_error);
void tui_append_diff(Tui *tui, const char *diff_text);
void tui_append_info(Tui *tui, const char *info_text);
void tui_append_error(Tui *tui, const char *err_text);

void tui_set_status(Tui *tui, const char *model, const char *backend, size_t tokens, size_t max_tokens, double tps, const char *status);
void tui_step_spinner(Tui *tui);

#ifdef __cplusplus
}
#endif

#endif /* SKEET_TUI_H */

