#include "tui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Tui *tui_new(const char *history_path) {
    Tui *tui = calloc(1, sizeof(Tui));
    if (!tui) return NULL;

    tui->is_interactive = term_is_interactive();
    tui->view = view_new();
    tui->input = input_editor_new(history_path);
    tui->stream_accum_cap = 2048;
    tui->stream_accum = malloc(tui->stream_accum_cap);
    tui->stream_accum_len = 0;
    if (tui->stream_accum) tui->stream_accum[0] = '\0';

    return tui;
}

void tui_free(Tui *tui) {
    if (!tui) return;
    tui_stop(tui);
    view_free(tui->view);
    input_editor_free(tui->input);
    free(tui->stream_accum);
    free(tui);
}

int tui_start(Tui *tui) {
    if (!tui || !tui->is_interactive) return 0;
    if (term_raw_enable() != 0) {
        tui->is_interactive = 0;
        return -1;
    }
    term_alt_screen_enter();
    tui->alt_screen_enabled = 1;
    tui_redraw(tui);
    return 0;
}

void tui_stop(Tui *tui) {
    if (!tui) return;
    if (tui->alt_screen_enabled) {
        term_alt_screen_leave();
        tui->alt_screen_enabled = 0;
    }
    if (tui->is_interactive) {
        term_raw_disable();
        term_cursor_show();
    }
}

void tui_redraw(Tui *tui) {
    if (!tui || !tui->is_interactive) return;
    TermSize sz = term_get_size();
    view_render(tui->view, sz, 1);
    input_editor_render(tui->input, sz, sz.rows, "\033[1;32mskeet>\033[0m ");
}

int tui_read_line(Tui *tui, const char *prompt, char *out_buf, size_t max_len) {
    if (!tui || !out_buf || max_len == 0) return -1;

    if (!tui->is_interactive) {
        /* Non-interactive fallback */
        printf("%s", prompt ? prompt : "skeet> ");
        fflush(stdout);
        if (!fgets(out_buf, (int)max_len, stdin)) {
            return -1;
        }
        size_t l = strlen(out_buf);
        while (l > 0 && (out_buf[l - 1] == '\n' || out_buf[l - 1] == '\r')) {
            out_buf[--l] = '\0';
        }
        return 0;
    }

    input_editor_clear(tui->input);
    view_scroll_bottom(tui->view);
    view_set_status(tui->view, NULL, NULL, 0, 0, 0, "ready");

    while (1) {
        TermSize sz = term_get_size();
        view_render(tui->view, sz, 1);
        input_editor_render(tui->input, sz, sz.rows, prompt ? prompt : "\033[1;32mskeet>\033[0m ");

        TermEvent ev;
        int r = term_read_event(&ev, 200);
        if (r < 0) {
            return -1;
        }
        if (r == 0) {
            continue; /* Timeout for updates */
        }

        /* View navigation keys */
        if (ev.key == KEY_PGUP) {
            view_scroll_page_up(tui->view, sz.rows - 2);
            continue;
        } else if (ev.key == KEY_PGDN) {
            view_scroll_page_down(tui->view, sz.rows - 2);
            continue;
        }

        int handled = input_editor_handle_key(tui->input, &ev);
        if (handled == 1) {
            const char *text = input_editor_get_text(tui->input);
            snprintf(out_buf, max_len, "%s", text);
            input_editor_history_add(tui->input, text);
            input_editor_clear(tui->input);
            view_scroll_bottom(tui->view);
            return 0;
        } else if (handled == -1) {
            /* Ctrl+C */
            return -1;
        }
    }
}

void tui_append_user_message(Tui *tui, const char *text) {
    if (!tui) return;
    view_append_line(tui->view, LINE_USER_HEADER, "─── You ───────────────────────────────────");
    view_append_text(tui->view, LINE_USER_TEXT, text);
    view_scroll_bottom(tui->view);
    tui_redraw(tui);
}

void tui_start_assistant_message(Tui *tui) {
    if (!tui) return;
    view_append_line(tui->view, LINE_ASSISTANT_HEADER, "─── Skeet ─────────────────────────────────");
    tui->stream_accum_len = 0;
    if (tui->stream_accum) tui->stream_accum[0] = '\0';
    view_set_status(tui->view, NULL, NULL, 0, 0, 0, "thinking");
    tui_redraw(tui);
}

void tui_stream_token(Tui *tui, const char *token) {
    if (!tui || !token) return;
    if (!tui->is_interactive) {
        fwrite(token, 1, strlen(token), stdout);
        fflush(stdout);
        return;
    }

    size_t tlen = strlen(token);
    if (tui->stream_accum_len + tlen + 1 >= tui->stream_accum_cap) {
        size_t ncap = tui->stream_accum_cap * 2 + tlen;
        char *nb = realloc(tui->stream_accum, ncap);
        if (nb) {
            tui->stream_accum = nb;
            tui->stream_accum_cap = ncap;
        }
    }

    if (tui->stream_accum_len + tlen + 1 < tui->stream_accum_cap) {
        memcpy(tui->stream_accum + tui->stream_accum_len, token, tlen);
        tui->stream_accum_len += tlen;
        tui->stream_accum[tui->stream_accum_len] = '\0';
    }

    /* If newline found or buffer reached threshold, push into view lines */
    if (strchr(token, '\n')) {
        view_append_text(tui->view, LINE_ASSISTANT_TEXT, tui->stream_accum);
        tui->stream_accum_len = 0;
        tui->stream_accum[0] = '\0';
    }

    view_step_spinner(tui->view);
    view_set_status(tui->view, NULL, NULL, 0, 0, 0, "streaming");
    view_scroll_bottom(tui->view);
    tui_redraw(tui);
}

void tui_end_assistant_message(Tui *tui) {
    if (!tui) return;
    if (tui->stream_accum_len > 0) {
        view_append_text(tui->view, LINE_ASSISTANT_TEXT, tui->stream_accum);
        tui->stream_accum_len = 0;
        tui->stream_accum[0] = '\0';
    }
    view_set_status(tui->view, NULL, NULL, 0, 0, 0, "idle");
    view_scroll_bottom(tui->view);
    tui_redraw(tui);
}

void tui_append_tool(Tui *tui, const char *tool_name, const char *args, const char *result, int is_error) {
    if (!tui) return;
    view_append_tool_box(tui->view, tool_name, args, result, is_error);
    view_scroll_bottom(tui->view);
    tui_redraw(tui);
}

void tui_append_diff(Tui *tui, const char *diff_text) {
    if (!tui) return;
    view_append_diff(tui->view, diff_text);
    view_scroll_bottom(tui->view);
    tui_redraw(tui);
}

void tui_append_info(Tui *tui, const char *info_text) {
    if (!tui) return;
    view_append_line(tui->view, LINE_INFO, info_text);
    view_scroll_bottom(tui->view);
    tui_redraw(tui);
}

void tui_append_error(Tui *tui, const char *err_text) {
    if (!tui) return;
    view_append_line(tui->view, LINE_ERROR, err_text);
    view_scroll_bottom(tui->view);
    tui_redraw(tui);
}

void tui_set_status(Tui *tui, const char *model, const char *backend, size_t tokens, size_t max_tokens, double tps, const char *status) {
    if (!tui) return;
    view_set_status(tui->view, model, backend, tokens, max_tokens, tps, status);
}

void tui_step_spinner(Tui *tui) {
    if (!tui) return;
    view_step_spinner(tui->view);
}

