#include "input.h"
#include "compat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#if defined(SKEET_OS_WINDOWS)
    #include <io.h>
#else
    #include <dirent.h>
    #include <sys/stat.h>
#endif

static const char *BUILTIN_COMMANDS[] = {
    "/help", "/tools", "/exit", "/quit", "/clear", "/model", "/backend", "/tune", "/history", "/compact", "/session"
};
static const size_t BUILTIN_COMMANDS_COUNT = sizeof(BUILTIN_COMMANDS) / sizeof(BUILTIN_COMMANDS[0]);

InputEditor *input_editor_new(const char *history_file) {
    InputEditor *ed = calloc(1, sizeof(InputEditor));
    if (!ed) return NULL;
    ed->history_index = -1;
    if (history_file) {
        ed->history_file = strdup(history_file);
        input_editor_history_load(ed);
    }
    return ed;
}

void input_editor_free(InputEditor *ed) {
    if (!ed) return;
    input_editor_history_save(ed);
    for (size_t i = 0; i < ed->history_count; i++) {
        free(ed->history[i]);
    }
    free(ed->history);
    free(ed->saved_draft);
    free(ed->history_file);
    free(ed);
}

void input_editor_clear(InputEditor *ed) {
    if (!ed) return;
    ed->buffer[0] = '\0';
    ed->len = 0;
    ed->cursor = 0;
    ed->history_index = -1;
    free(ed->saved_draft);
    ed->saved_draft = NULL;
}

void input_editor_set_text(InputEditor *ed, const char *text) {
    if (!ed) return;
    if (!text) {
        input_editor_clear(ed);
        return;
    }
    size_t l = strlen(text);
    if (l >= sizeof(ed->buffer)) l = sizeof(ed->buffer) - 1;
    memcpy(ed->buffer, text, l);
    ed->buffer[l] = '\0';
    ed->len = l;
    ed->cursor = l;
}

const char *input_editor_get_text(const InputEditor *ed) {
    return ed ? ed->buffer : "";
}

void input_editor_history_add(InputEditor *ed, const char *line) {
    if (!ed || !line || line[0] == '\0') return;
    if (ed->history_count > 0 && strcmp(ed->history[ed->history_count - 1], line) == 0) {
        return; /* skip consecutive duplicates */
    }

    if (ed->history_count >= ed->history_cap) {
        size_t new_cap = ed->history_cap ? ed->history_cap * 2 : 32;
        char **nh = realloc(ed->history, new_cap * sizeof(char *));
        if (!nh) return;
        ed->history = nh;
        ed->history_cap = new_cap;
    }
    ed->history[ed->history_count++] = strdup(line);
}

void input_editor_history_load(InputEditor *ed) {
    if (!ed || !ed->history_file) return;
    FILE *f = fopen(ed->history_file, "r");
    if (!f) return;

    char line[4096];
    while (fgets(line, sizeof(line), f)) {
        size_t l = strlen(line);
        while (l > 0 && (line[l - 1] == '\n' || line[l - 1] == '\r')) {
            line[--l] = '\0';
        }
        if (l > 0) {
            input_editor_history_add(ed, line);
        }
    }
    fclose(f);
}

void input_editor_history_save(InputEditor *ed) {
    if (!ed || !ed->history_file) return;

    /* Make parent directory if needed */
    char *dup = strdup(ed->history_file);
    char *last_slash = strrchr(dup, '/');
    if (!last_slash) last_slash = strrchr(dup, '\\');
    if (last_slash) {
        *last_slash = '\0';
        skeet_mkdir(dup, 0755);
    }
    free(dup);

    FILE *f = fopen(ed->history_file, "w");
    if (!f) return;

    size_t start = ed->history_count > 1000 ? ed->history_count - 1000 : 0;
    for (size_t i = start; i < ed->history_count; i++) {
        fprintf(f, "%s\n", ed->history[i]);
    }
    fclose(f);
}

static void complete_token(InputEditor *ed) {
    if (ed->len == 0) return;

    if (ed->buffer[0] == '/') {
        /* Command completion */
        const char *prefix = ed->buffer;
        size_t plen = ed->len;
        const char *match = NULL;
        int match_count = 0;

        for (size_t i = 0; i < BUILTIN_COMMANDS_COUNT; i++) {
            if (strncmp(BUILTIN_COMMANDS[i], prefix, plen) == 0) {
                match = BUILTIN_COMMANDS[i];
                match_count++;
            }
        }

        if (match_count == 1 && match) {
            input_editor_set_text(ed, match);
            /* Add space after completed slash command */
            if (ed->len < sizeof(ed->buffer) - 2) {
                ed->buffer[ed->len++] = ' ';
                ed->buffer[ed->len] = '\0';
                ed->cursor = ed->len;
            }
        }
    }
}

int input_editor_handle_key(InputEditor *ed, const TermEvent *ev) {
    if (!ed || !ev) return 0;

    switch (ev->key) {
    case KEY_ENTER:
        return 1;

    case KEY_CTRL_C:
        return -1;

    case KEY_BACKSPACE:
        if (ed->cursor > 0) {
            memmove(&ed->buffer[ed->cursor - 1], &ed->buffer[ed->cursor], ed->len - ed->cursor + 1);
            ed->cursor--;
            ed->len--;
        }
        return 0;

    case KEY_DELETE:
        if (ed->cursor < ed->len) {
            memmove(&ed->buffer[ed->cursor], &ed->buffer[ed->cursor + 1], ed->len - ed->cursor);
            ed->len--;
        }
        return 0;

    case KEY_LEFT:
        if (ed->cursor > 0) ed->cursor--;
        return 0;

    case KEY_RIGHT:
        if (ed->cursor < ed->len) ed->cursor++;
        return 0;

    case KEY_HOME:
    case KEY_CTRL_A:
        ed->cursor = 0;
        return 0;

    case KEY_END:
    case KEY_CTRL_E:
        ed->cursor = ed->len;
        return 0;

    case KEY_CTRL_K:
        /* Kill to end of line */
        ed->buffer[ed->cursor] = '\0';
        ed->len = ed->cursor;
        return 0;

    case KEY_CTRL_U:
        /* Clear line */
        ed->buffer[0] = '\0';
        ed->len = 0;
        ed->cursor = 0;
        return 0;

    case KEY_CTRL_W:
        /* Delete backward word */
        if (ed->cursor > 0) {
            size_t p = ed->cursor;
            while (p > 0 && isspace((unsigned char)ed->buffer[p - 1])) p--;
            while (p > 0 && !isspace((unsigned char)ed->buffer[p - 1])) p--;
            size_t del_count = ed->cursor - p;
            memmove(&ed->buffer[p], &ed->buffer[ed->cursor], ed->len - ed->cursor + 1);
            ed->len -= del_count;
            ed->cursor = p;
        }
        return 0;

    case KEY_CTRL_LEFT:
        if (ed->cursor > 0) {
            while (ed->cursor > 0 && isspace((unsigned char)ed->buffer[ed->cursor - 1])) ed->cursor--;
            while (ed->cursor > 0 && !isspace((unsigned char)ed->buffer[ed->cursor - 1])) ed->cursor--;
        }
        return 0;

    case KEY_CTRL_RIGHT:
        if (ed->cursor < ed->len) {
            while (ed->cursor < ed->len && !isspace((unsigned char)ed->buffer[ed->cursor])) ed->cursor++;
            while (ed->cursor < ed->len && isspace((unsigned char)ed->buffer[ed->cursor])) ed->cursor++;
        }
        return 0;

    case KEY_UP:
        if (ed->history_count == 0) return 0;
        if (ed->history_index == -1) {
            free(ed->saved_draft);
            ed->saved_draft = strdup(ed->buffer);
            ed->history_index = (int)ed->history_count - 1;
        } else if (ed->history_index > 0) {
            ed->history_index--;
        }
        if (ed->history_index >= 0 && ed->history_index < (int)ed->history_count) {
            input_editor_set_text(ed, ed->history[ed->history_index]);
        }
        return 0;

    case KEY_DOWN:
        if (ed->history_index == -1) return 0;
        if (ed->history_index + 1 < (int)ed->history_count) {
            ed->history_index++;
            input_editor_set_text(ed, ed->history[ed->history_index]);
        } else {
            ed->history_index = -1;
            input_editor_set_text(ed, ed->saved_draft ? ed->saved_draft : "");
        }
        return 0;

    case KEY_TAB:
        complete_token(ed);
        return 0;

    case KEY_CHAR:
        if (ed->len + 1 < sizeof(ed->buffer) && ev->ch >= 32) {
            memmove(&ed->buffer[ed->cursor + 1], &ed->buffer[ed->cursor], ed->len - ed->cursor + 1);
            ed->buffer[ed->cursor] = (char)ev->ch;
            ed->cursor++;
            ed->len++;
        }
        return 0;

    default:
        break;
    }

    return 0;
}

void input_editor_render(InputEditor *ed, TermSize size, int prompt_row, const char *prompt) {
    if (!ed) return;
    term_cursor_move(prompt_row, 1);
    term_clear_line();

    const char *p = prompt ? prompt : "skeet> ";
    printf("%s", p);

    /* Print text with horizontal scroll if line exceeds terminal width */
    int prompt_len = (int)strlen(p);
    int avail_cols = size.cols - prompt_len - 1;
    if (avail_cols < 10) avail_cols = 10;

    int offset = 0;
    if ((int)ed->cursor >= avail_cols) {
        offset = (int)ed->cursor - avail_cols + 5;
    }

    int visible_len = (int)ed->len - offset;
    if (visible_len > avail_cols) visible_len = avail_cols;
    if (visible_len < 0) visible_len = 0;

    printf("%.*s", visible_len, ed->buffer + offset);

    /* Position cursor */
    int cur_col = prompt_len + 1 + ((int)ed->cursor - offset);
    term_cursor_move(prompt_row, cur_col);
    fflush(stdout);
}

