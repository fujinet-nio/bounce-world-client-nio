#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

#include "conio.h"
#include "screen.h"

static struct termios saved_termios;
static uint8_t termios_saved = 0;
static uint8_t raw_mode_enabled = 0;
static uint8_t cursor_x = 0;
static uint8_t cursor_y = 0;

typedef struct { char glyph[5]; uint8_t reverse; } Cell;
static Cell cells[SCREEN_HEIGHT][SCREEN_WIDTH];
static Cell shown[SCREEN_HEIGHT][SCREEN_WIDTH];
static uint8_t in_frame, reverse_on, shown_valid;
static volatile sig_atomic_t interrupted;

static void on_signal(int sig) { interrupted = sig; }

void conio_clear_rows(uint8_t rows)
{
    unsigned x, y;
    if (rows > SCREEN_HEIGHT) rows = SCREEN_HEIGHT;
    for (y = 0; y < rows; ++y) {
        for (x = 0; x < SCREEN_WIDTH; ++x) {
            memset(&cells[y][x], 0, sizeof(Cell));
            cells[y][x].glyph[0] = ' ';
        }
        if (!in_frame) fprintf(stdout, "\033[%u;1H\033[2K", y + 1);
    }
    if (!in_frame) { shown_valid = 0; fflush(stdout); }
    cursor_x = cursor_y = 0;
}

void conio_begin_frame(void) { in_frame = 1; }

void conio_end_frame(void)
{
    unsigned x, y;
    int attr = -1;
    /* Disable wrapping while touching the bottom-right cell: it must not
     * scroll the status bar away on an exactly 80x24 terminal. */
    fputs("\033[?7l", stdout);
    for (y = 0; y < SCREEN_HEIGHT; ++y) {
        int contiguous = 0;
        for (x = 0; x < SCREEN_WIDTH; ++x) {
            Cell *c = &cells[y][x];
            if (shown_valid && !memcmp(c, &shown[y][x], sizeof(Cell))) {
                contiguous = 0;
                continue;
            }
            if (!contiguous) fprintf(stdout, "\033[%u;%uH", y + 1, x + 1);
            if (attr != c->reverse) {
                fputs(c->reverse ? "\033[7m" : "\033[27m", stdout);
                attr = c->reverse;
            }
            fputs(c->glyph[0] ? c->glyph : " ", stdout);
            contiguous = 1;
        }
    }
    fputs("\033[0m\033[?7h", stdout);
    fflush(stdout);
    memcpy(shown, cells, sizeof(cells));
    shown_valid = 1;
    in_frame = 0;
}

static void restore_terminal(void)
{
    if (termios_saved && raw_mode_enabled) {
        tcsetattr(STDIN_FILENO, TCSANOW, &saved_termios);
        raw_mode_enabled = 0;
    }

    fputs("\033[0m\033[?7h\033[?25h", stdout);
    fflush(stdout);
}

static void ensure_terminal_mode(void)
{
    struct termios raw;

    if (!isatty(STDIN_FILENO)) {
        return;
    }

    if (!termios_saved) {
        if (tcgetattr(STDIN_FILENO, &saved_termios) != 0) {
            return;
        }
        termios_saved = 1;
        atexit(restore_terminal);
        signal(SIGINT, on_signal);
        signal(SIGTERM, on_signal);
    }

    if (raw_mode_enabled) {
        return;
    }

    raw = saved_termios;
    raw.c_iflag &= (tcflag_t)~(ICRNL | IXON);
    raw.c_lflag &= (tcflag_t)~(ECHO | ICANON);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) == 0) {
        raw_mode_enabled = 1;
    }
}

static void update_cursor_after_char(char c)
{
    if (c == '\r') {
        cursor_x = 0;
    } else if (c == '\n') {
        cursor_x = 0;
        cursor_y++;
    } else {
        cursor_x++;
    }
}

static void output_cell(const char *glyph)
{
    if (in_frame) {
        if (cursor_x < SCREEN_WIDTH && cursor_y < SCREEN_HEIGHT) {
            Cell *c = &cells[cursor_y][cursor_x];
            memset(c, 0, sizeof(*c));
            strncpy(c->glyph, glyph, sizeof(c->glyph) - 1);
            c->reverse = reverse_on;
        }
    } else {
        fputs(glyph, stdout);
        fflush(stdout);
        shown_valid = 0;
    }
    cursor_x++;
}

void clrscr(void)
{
    if (in_frame) { conio_clear_rows(SCREEN_HEIGHT); return; }
    shown_valid = 0;
    fputs("\033[2J\033[H", stdout);
    fflush(stdout);
    cursor_x = 0;
    cursor_y = 0;
}

void gotoxy(uint8_t x, uint8_t y)
{
    if (!in_frame) {
        fprintf(stdout, "\033[%u;%uH", (unsigned int)y + 1U, (unsigned int)x + 1U);
        fflush(stdout);
    }
    cursor_x = x;
    cursor_y = y;
}

void gotox(uint8_t x)
{
    gotoxy(x, cursor_y);
}

uint8_t wherex(void)
{
    return cursor_x;
}

uint8_t wherey(void)
{
    return cursor_y;
}

void cputc(char c)
{
    switch ((unsigned char)c) {
        case (unsigned char)CH_ULCORNER: output_cell("┌"); break;
        case (unsigned char)CH_URCORNER: output_cell("┐"); break;
        case (unsigned char)CH_LLCORNER: output_cell("└"); break;
        case (unsigned char)CH_LRCORNER: output_cell("┘"); break;
        case (unsigned char)CH_HLINE:    output_cell("─"); break;
        case (unsigned char)CH_VLINE:    output_cell("│"); break;
        case (unsigned char)CH_CROSS:    output_cell("┼"); break;
        case (unsigned char)CH_LTEE:     output_cell("├"); break;
        case (unsigned char)CH_RTEE:     output_cell("┤"); break;
        case (unsigned char)CH_TTEE:     output_cell("┬"); break;
        case (unsigned char)CH_BTEE:     output_cell("┴"); break;
        case (unsigned char)CH_LHALF:    output_cell("▌"); break;
        case (unsigned char)CH_RHALF:    output_cell("▐"); break;
        case (unsigned char)CH_BOTTOM:   output_cell("▄"); break;
        case (unsigned char)CH_TOP:      output_cell("▀"); break;
        case (unsigned char)CH_LLQUAD:   output_cell("▖"); break;
        case (unsigned char)CH_LRQUAD:   output_cell("▗"); break;
        case (unsigned char)CH_ULQUAD:   output_cell("▘"); break;
        case (unsigned char)CH_URQUAD:   output_cell("▝"); break;
        case (unsigned char)CH_URBLOCK:  output_cell("▜"); break;
        case (unsigned char)CH_ULBLOCK:  output_cell("▛"); break;
        case (unsigned char)CH_LRBLOCK:  output_cell("▟"); break;
        case (unsigned char)CH_LLBLOCK:  output_cell("▙"); break;
        case (unsigned char)CH_FULLBLOCK: output_cell("█"); break;
        case (unsigned char)CH_CROSS_DIAG: output_cell("▚"); break;
        case (unsigned char)CH_CROSS_REV:  output_cell("▞"); break;
        default:
            if (c == '\r' || c == '\n') {
                if (!in_frame) { fputc(c, stdout); fflush(stdout); }
                update_cursor_after_char(c);
            } else {
                char glyph[2] = {c, 0};
                output_cell(glyph);
            }
            break;
    }
}

void cputs(const char *s)
{
    while (*s != '\0') {
        cputc(*s++);
    }
}

void cputcxy(uint8_t x, uint8_t y, char c)
{
    gotoxy(x, y);
    cputc(c);
}

void cputsxy(uint8_t x, uint8_t y, const char *s)
{
    gotoxy(x, y);
    cputs(s);
}

void revers(uint8_t on)
{
    reverse_on = !!on;
    if (in_frame) return;
    fputs(on ? "\033[7m" : "\033[27m", stdout);
    fflush(stdout);
}

void cursor(uint8_t on)
{
    fputs(on ? "\033[?25h" : "\033[?25l", stdout);
    fflush(stdout);
}

void chlinexy(uint8_t x, uint8_t y, uint8_t len)
{
    uint8_t i;

    gotoxy(x, y);
    for (i = 0; i < len; ++i) {
        cputc(CH_HLINE);
    }
}

uint8_t kbhit(void)
{
    fd_set read_fds;
    struct timeval timeout;
    int result;

    ensure_terminal_mode();
    if (interrupted) exit(128 + interrupted);

    FD_ZERO(&read_fds);
    FD_SET(STDIN_FILENO, &read_fds);
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;

    result = select(STDIN_FILENO + 1, &read_fds, NULL, NULL, &timeout);
    return (result > 0) ? 1U : 0U;
}

char cgetc(void)
{
    unsigned char c = 0;
    ssize_t nread;

    ensure_terminal_mode();
    if (interrupted) exit(128 + interrupted);

    do {
        nread = read(STDIN_FILENO, &c, 1);
    } while (nread < 0 && errno == EINTR && !interrupted);
    if (interrupted) exit(128 + interrupted);

    if (nread <= 0) {
        return '\0';
    }

    if (c == '\n') {
        return (char)CH_ENTER;
    }
    if (c == 0x7fU) {
        return (char)CH_DEL;
    }

    return (char)c;
}
