#ifndef SKEET_TUI_TERM_H
#define SKEET_TUI_TERM_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum TermKey {
    KEY_NONE = 0,
    KEY_CHAR,
    KEY_ENTER,
    KEY_BACKSPACE,
    KEY_TAB,
    KEY_ESC,
    KEY_UP,
    KEY_DOWN,
    KEY_LEFT,
    KEY_RIGHT,
    KEY_HOME,
    KEY_END,
    KEY_PGUP,
    KEY_PGDN,
    KEY_DELETE,
    KEY_CTRL_A,
    KEY_CTRL_C,
    KEY_CTRL_D,
    KEY_CTRL_E,
    KEY_CTRL_K,
    KEY_CTRL_L,
    KEY_CTRL_U,
    KEY_CTRL_W,
    KEY_CTRL_LEFT,
    KEY_CTRL_RIGHT
} TermKey;

typedef struct TermEvent {
    TermKey key;
    uint32_t ch; /* Unicode codepoint or ASCII char */
} TermEvent;

typedef struct TermSize {
    int rows;
    int cols;
} TermSize;

int term_is_interactive(void);
int term_raw_enable(void);
void term_raw_disable(void);
int term_is_raw(void);

void term_alt_screen_enter(void);
void term_alt_screen_leave(void);

TermSize term_get_size(void);

void term_cursor_hide(void);
void term_cursor_show(void);
void term_cursor_move(int row, int col);
void term_clear_screen(void);
void term_clear_line(void);
void term_clear_to_end(void);

/* Reads a key event. Returns 1 if key read, 0 if no input / timeout, -1 on EOF or error. */
int term_read_event(TermEvent *out_ev, int timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* SKEET_TUI_TERM_H */

