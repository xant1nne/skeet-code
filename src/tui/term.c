#include "term.h"
#include "compat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(SKEET_OS_WINDOWS)
    #include <windows.h>
    #include <io.h>
    #define isatty _isatty
    #define fileno _fileno
#else
    #include <termios.h>
    #include <unistd.h>
    #include <sys/ioctl.h>
    #include <poll.h>
    #include <errno.h>
#endif

static int g_raw_active = 0;
static int g_alt_active = 0;

#if !defined(SKEET_OS_WINDOWS)
static struct termios g_orig_termios;
#else
static DWORD g_orig_in_mode = 0;
static DWORD g_orig_out_mode = 0;
#endif

int term_is_interactive(void) {
#if defined(SKEET_OS_WINDOWS)
    return _isatty(_fileno(stdin)) && _isatty(_fileno(stdout));
#else
    return isatty(STDIN_FILENO) && isatty(STDOUT_FILENO);
#endif
}

int term_raw_enable(void) {
    if (g_raw_active) return 0;
    if (!term_is_interactive()) return -1;

#if defined(SKEET_OS_WINDOWS)
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hIn == INVALID_HANDLE_VALUE || hOut == INVALID_HANDLE_VALUE) return -1;

    GetConsoleMode(hIn, &g_orig_in_mode);
    GetConsoleMode(hOut, &g_orig_out_mode);

    DWORD in_mode = g_orig_in_mode;
    in_mode &= ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT);
    in_mode |= ENABLE_VIRTUAL_TERMINAL_INPUT;
    SetConsoleMode(hIn, in_mode);

    DWORD out_mode = g_orig_out_mode;
    out_mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING | DISABLE_NEWLINE_AUTO_RETURN;
    SetConsoleMode(hOut, out_mode);
#else
    if (tcgetattr(STDIN_FILENO, &g_orig_termios) == -1) {
        return -1;
    }

    struct termios raw = g_orig_termios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        return -1;
    }
#endif

    g_raw_active = 1;
    return 0;
}

void term_raw_disable(void) {
    if (!g_raw_active) return;

#if defined(SKEET_OS_WINDOWS)
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hIn != INVALID_HANDLE_VALUE) SetConsoleMode(hIn, g_orig_in_mode);
    if (hOut != INVALID_HANDLE_VALUE) SetConsoleMode(hOut, g_orig_out_mode);
#else
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_orig_termios);
#endif

    g_raw_active = 0;
}

int term_is_raw(void) {
    return g_raw_active;
}

void term_alt_screen_enter(void) {
    if (!g_alt_active) {
        printf("\033[?1049h\033[H");
        fflush(stdout);
        g_alt_active = 1;
    }
}

void term_alt_screen_leave(void) {
    if (g_alt_active) {
        printf("\033[?1049l");
        fflush(stdout);
        g_alt_active = 0;
    }
}

TermSize term_get_size(void) {
    TermSize sz = { .rows = 24, .cols = 80 };

#if defined(SKEET_OS_WINDOWS)
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut != INVALID_HANDLE_VALUE) {
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        if (GetConsoleScreenBufferInfo(hOut, &csbi)) {
            sz.cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
            sz.rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
        }
    }
#else
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) {
        sz.cols = ws.ws_col;
        sz.rows = ws.ws_row;
    } else {
        const char *lines = getenv("LINES");
        const char *cols = getenv("COLUMNS");
        if (lines) sz.rows = atoi(lines);
        if (cols) sz.cols = atoi(cols);
    }
#endif

    if (sz.cols < 20) sz.cols = 20;
    if (sz.rows < 5) sz.rows = 5;
    return sz;
}

void term_cursor_hide(void) {
    printf("\033[?25l");
    fflush(stdout);
}

void term_cursor_show(void) {
    printf("\033[?25h");
    fflush(stdout);
}

void term_cursor_move(int row, int col) {
    printf("\033[%d;%dH", row, col);
    fflush(stdout);
}

void term_clear_screen(void) {
    printf("\033[2J\033[H");
    fflush(stdout);
}

void term_clear_line(void) {
    printf("\033[2K\r");
    fflush(stdout);
}

void term_clear_to_end(void) {
    printf("\033[0J");
    fflush(stdout);
}

#if defined(SKEET_OS_WINDOWS)
static int read_byte_timed(unsigned char *b, int timeout_ms) {
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    if (hIn == INVALID_HANDLE_VALUE) return -1;

    DWORD wait_res = WaitForSingleObject(hIn, timeout_ms < 0 ? INFINITE : (DWORD)timeout_ms);
    if (wait_res == WAIT_OBJECT_0) {
        INPUT_RECORD rec;
        DWORD read_count = 0;
        while (PeekConsoleInputA(hIn, &rec, 1, &read_count) && read_count > 0) {
            ReadConsoleInputA(hIn, &rec, 1, &read_count);
            if (rec.EventType == KEY_EVENT && rec.Event.KeyEvent.bKeyDown) {
                CHAR ch = rec.Event.KeyEvent.uChar.AsciiChar;
                if (ch != 0) {
                    *b = (unsigned char)ch;
                    return 1;
                }
                /* Virtual key translation */
                switch (rec.Event.KeyEvent.wVirtualKeyCode) {
                case VK_UP:     *b = 0xE0; return 2; /* marker */
                case VK_DOWN:   *b = 0xE1; return 2;
                case VK_RIGHT:  *b = 0xE2; return 2;
                case VK_LEFT:   *b = 0xE3; return 2;
                case VK_HOME:   *b = 0xE4; return 2;
                case VK_END:    *b = 0xE5; return 2;
                case VK_PRIOR:  *b = 0xE6; return 2;
                case VK_NEXT:   *b = 0xE7; return 2;
                case VK_DELETE: *b = 0xE8; return 2;
                default: break;
                }
            }
        }
        return 0;
    }
    return 0;
}
#else
static int read_byte_timed(unsigned char *b, int timeout_ms) {
    struct pollfd pfd = { .fd = STDIN_FILENO, .events = POLLIN, .revents = 0 };
    int ret = poll(&pfd, 1, timeout_ms);
    if (ret > 0 && (pfd.revents & POLLIN)) {
        ssize_t n = read(STDIN_FILENO, b, 1);
        if (n == 1) return 1;
        if (n == 0) return -1;
    } else if (ret < 0 && errno != EINTR) {
        return -1;
    }
    return 0;
}
#endif

int term_read_event(TermEvent *out_ev, int timeout_ms) {
    if (!out_ev) return -1;
    memset(out_ev, 0, sizeof(*out_ev));

    unsigned char c = 0;
    int r = read_byte_timed(&c, timeout_ms);
    if (r <= 0) return r;

#if defined(SKEET_OS_WINDOWS)
    if (r == 2) {
        switch (c) {
        case 0xE0: out_ev->key = KEY_UP; return 1;
        case 0xE1: out_ev->key = KEY_DOWN; return 1;
        case 0xE2: out_ev->key = KEY_RIGHT; return 1;
        case 0xE3: out_ev->key = KEY_LEFT; return 1;
        case 0xE4: out_ev->key = KEY_HOME; return 1;
        case 0xE5: out_ev->key = KEY_END; return 1;
        case 0xE6: out_ev->key = KEY_PGUP; return 1;
        case 0xE7: out_ev->key = KEY_PGDN; return 1;
        case 0xE8: out_ev->key = KEY_DELETE; return 1;
        default: break;
        }
    }
#endif

    if (c == '\r' || c == '\n') {
        out_ev->key = KEY_ENTER;
        return 1;
    } else if (c == 127 || c == 8) {
        out_ev->key = KEY_BACKSPACE;
        return 1;
    } else if (c == '\t') {
        out_ev->key = KEY_TAB;
        return 1;
    } else if (c == 1) {
        out_ev->key = KEY_CTRL_A;
        return 1;
    } else if (c == 3) {
        out_ev->key = KEY_CTRL_C;
        return 1;
    } else if (c == 4) {
        out_ev->key = KEY_CTRL_D;
        return 1;
    } else if (c == 5) {
        out_ev->key = KEY_CTRL_E;
        return 1;
    } else if (c == 11) {
        out_ev->key = KEY_CTRL_K;
        return 1;
    } else if (c == 12) {
        out_ev->key = KEY_CTRL_L;
        return 1;
    } else if (c == 21) {
        out_ev->key = KEY_CTRL_U;
        return 1;
    } else if (c == 23) {
        out_ev->key = KEY_CTRL_W;
        return 1;
    } else if (c == 27) {
        /* Escape sequence parsing */
        unsigned char seq[8];
        if (read_byte_timed(&seq[0], 50) <= 0) {
            out_ev->key = KEY_ESC;
            return 1;
        }

        if (seq[0] == '[' || seq[0] == 'O') {
            if (read_byte_timed(&seq[1], 50) <= 0) {
                out_ev->key = KEY_ESC;
                return 1;
            }

            if (seq[0] == '[') {
                if (seq[1] >= '0' && seq[1] <= '9') {
                    if (read_byte_timed(&seq[2], 50) <= 0) {
                        out_ev->key = KEY_ESC;
                        return 1;
                    }
                    if (seq[2] == '~') {
                        switch (seq[1]) {
                        case '1': out_ev->key = KEY_HOME; return 1;
                        case '3': out_ev->key = KEY_DELETE; return 1;
                        case '4': out_ev->key = KEY_END; return 1;
                        case '5': out_ev->key = KEY_PGUP; return 1;
                        case '6': out_ev->key = KEY_PGDN; return 1;
                        case '7': out_ev->key = KEY_HOME; return 1;
                        case '8': out_ev->key = KEY_END; return 1;
                        default: break;
                        }
                    } else if (seq[1] == '1' && seq[2] == ';') {
                        unsigned char mod = 0, dir = 0;
                        if (read_byte_timed(&mod, 50) > 0 && read_byte_timed(&dir, 50) > 0) {
                            if (mod == '5') { /* Ctrl + Arrow */
                                if (dir == 'C') { out_ev->key = KEY_CTRL_RIGHT; return 1; }
                                if (dir == 'D') { out_ev->key = KEY_CTRL_LEFT; return 1; }
                            }
                        }
                    }
                } else {
                    switch (seq[1]) {
                    case 'A': out_ev->key = KEY_UP; return 1;
                    case 'B': out_ev->key = KEY_DOWN; return 1;
                    case 'C': out_ev->key = KEY_RIGHT; return 1;
                    case 'D': out_ev->key = KEY_LEFT; return 1;
                    case 'H': out_ev->key = KEY_HOME; return 1;
                    case 'F': out_ev->key = KEY_END; return 1;
                    default: break;
                    }
                }
            } else if (seq[0] == 'O') {
                switch (seq[1]) {
                case 'H': out_ev->key = KEY_HOME; return 1;
                case 'F': out_ev->key = KEY_END; return 1;
                default: break;
                }
            }
        }
        out_ev->key = KEY_ESC;
        return 1;
    }

    out_ev->key = KEY_CHAR;
    out_ev->ch = c;
    return 1;
}

