#include "tui/term.h"
#include "tui/view.h"
#include "tui/input.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_view_operations(void) {
    View *v = view_new();
    assert(v != NULL);
    assert(v->line_count == 0);

    view_append_line(v, LINE_NORMAL, "Hello world");
    view_append_line(v, LINE_USER_HEADER, "User Header");
    assert(v->line_count == 2);

    view_append_tool_box(v, "read_file", "{\"path\":\"foo.c\"}", "int x = 42;", 0);
    assert(v->line_count > 2);

    view_append_diff(v, "--- a/foo.c\n+++ b/foo.c\n@@ -1,1 +1,1 @@\n-old\n+new\n");
    assert(v->line_count > 5);

    view_set_status(v, "test_model", "test_backend", 100, 2048, 45.5, "streaming");
    assert(strcmp(v->status.model, "test_model") == 0);
    assert(v->status.tokens_used == 100);

    view_scroll_up(v, 5);
    assert(v->scroll_offset == 5);
    view_scroll_down(v, 2);
    assert(v->scroll_offset == 3);
    view_scroll_bottom(v);
    assert(v->scroll_offset == 0);

    view_free(v);
    printf("test_view_operations: PASS\n");
}

static void test_input_editor_operations(void) {
    InputEditor *ed = input_editor_new("build/test_hist.txt");
    assert(ed != NULL);

    input_editor_set_text(ed, "hello");
    assert(strcmp(input_editor_get_text(ed), "hello") == 0);
    assert(ed->cursor == 5);

    /* Backspace */
    TermEvent ev_bs = { .key = KEY_BACKSPACE };
    input_editor_handle_key(ed, &ev_bs);
    assert(strcmp(input_editor_get_text(ed), "hell") == 0);
    assert(ed->cursor == 4);

    /* Left Arrow */
    TermEvent ev_left = { .key = KEY_LEFT };
    input_editor_handle_key(ed, &ev_left);
    assert(ed->cursor == 3);

    /* Insert char */
    TermEvent ev_char = { .key = KEY_CHAR, .ch = 'p' };
    input_editor_handle_key(ed, &ev_char);
    assert(strcmp(input_editor_get_text(ed), "helpl") == 0);

    /* Home & End */
    TermEvent ev_home = { .key = KEY_HOME };
    input_editor_handle_key(ed, &ev_home);
    assert(ed->cursor == 0);

    TermEvent ev_end = { .key = KEY_END };
    input_editor_handle_key(ed, &ev_end);
    assert(ed->cursor == 5);

    /* Tab completion on slash command */
    input_editor_set_text(ed, "/he");
    TermEvent ev_tab = { .key = KEY_TAB };
    input_editor_handle_key(ed, &ev_tab);
    assert(strcmp(input_editor_get_text(ed), "/help ") == 0);

    /* History */
    input_editor_history_add(ed, "first_cmd");
    input_editor_history_add(ed, "second_cmd");
    assert(ed->history_count == 2);

    input_editor_clear(ed);
    TermEvent ev_up = { .key = KEY_UP };
    input_editor_handle_key(ed, &ev_up);
    assert(strcmp(input_editor_get_text(ed), "second_cmd") == 0);

    input_editor_handle_key(ed, &ev_up);
    assert(strcmp(input_editor_get_text(ed), "first_cmd") == 0);

    TermEvent ev_down = { .key = KEY_DOWN };
    input_editor_handle_key(ed, &ev_down);
    assert(strcmp(input_editor_get_text(ed), "second_cmd") == 0);

    input_editor_free(ed);
    printf("test_input_editor_operations: PASS\n");
}

int main(void) {
    test_view_operations();
    test_input_editor_operations();
    printf("test_tui: PASS\n");
    return 0;
}

