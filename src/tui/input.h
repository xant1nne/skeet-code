#ifndef SKEET_TUI_INPUT_H
#define SKEET_TUI_INPUT_H

#include "term.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct InputEditor {
    char buffer[8192];
    size_t len;
    size_t cursor;

    char **history;
    size_t history_count;
    size_t history_cap;
    int history_index; /* -1 = drafting current, >=0 = browsing */
    char *saved_draft;

    char *history_file;
} InputEditor;

InputEditor *input_editor_new(const char *history_file);
void input_editor_free(InputEditor *ed);

void input_editor_clear(InputEditor *ed);
void input_editor_set_text(InputEditor *ed, const char *text);
const char *input_editor_get_text(const InputEditor *ed);

void input_editor_history_add(InputEditor *ed, const char *line);
void input_editor_history_load(InputEditor *ed);
void input_editor_history_save(InputEditor *ed);

/* Returns 1 if Enter pressed (submitted), 0 if handled/continued, -1 on cancel/exit */
int input_editor_handle_key(InputEditor *ed, const TermEvent *ev);

/* Render prompt and editor buffer at prompt_row */
void input_editor_render(InputEditor *ed, TermSize size, int prompt_row, const char *prompt);

#ifdef __cplusplus
}
#endif

#endif /* SKEET_TUI_INPUT_H */

