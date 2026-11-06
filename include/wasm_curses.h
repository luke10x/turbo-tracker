#pragma once

/*
 * Minimal ncurses-compatible drawing API for xterm.js.
 *
 * Include this file normally to use the declarations. In exactly one source
 * file, define WASM_CURSES_IMPLEMENTATION before including it to emit the
 * implementation:
 *
 *     #define WASM_CURSES_IMPLEMENTATION
 *     #include "wasm_curses.h"
 */

#define TRUE 1
#define FALSE 0
#define ERR (-1)
#define OK 0
#define A_BOLD 0x100
#define COLOR_BLACK 0
#define COLOR_RED 1
#define COLOR_GREEN 2
#define COLOR_YELLOW 3
#define COLOR_BLUE 4
#define COLOR_MAGENTA 5
#define COLOR_CYAN 6
#define COLOR_WHITE 7
#define COLOR_PAIR(n) (n)
#define KEY_UP 1001
#define KEY_DOWN 1002
#define KEY_LEFT 1003
#define KEY_RIGHT 1004
#define KEY_HOME 1005
#define KEY_END 1006
#define KEY_PPAGE 1007
#define KEY_NPAGE 1008
#define KEY_BACKSPACE 1009
#define KEY_DC 1010
#define KEY_BTAB 1011
#define KEY_MOUSE 1100
#define KEY_RESIZE 1101
#define KEY_F(n) (2000 + (n))
#define BUTTON1_CLICKED (1UL << 0)
#define BUTTON4_PRESSED (1UL << 1)
#define BUTTON5_PRESSED (1UL << 2)
#define ALL_MOUSE_EVENTS (BUTTON1_CLICKED | BUTTON4_PRESSED | BUTTON5_PRESSED)

typedef unsigned long mmask_t;
typedef struct {
    short id;
    int x, y, z;
    mmask_t bstate;
} MEVENT;

typedef struct {
    int height;
    int width;
    int y;
    int x;
    int color_pair;
    int bold;
} WINDOW;

#ifdef __cplusplus
extern "C" {
#endif

extern WINDOW wasm_stdscr;
#define stdscr (&wasm_stdscr)

WINDOW *initscr(void);
int endwin(void);
int erase(void);
int refresh(void);
int doupdate(void);
int cbreak(void);
int noecho(void);
int keypad(WINDOW *win, int enabled);
int nodelay(WINDOW *win, int enabled);
void timeout(int delay);
int getch(void);
mmask_t mousemask(mmask_t mask, mmask_t *oldmask);
int getmouse(MEVENT *event);
int curs_set(int visible);
int start_color(void);
int use_default_colors(void);
int init_pair(short pair, short fg, short bg);
WINDOW *newwin(int height, int width, int y, int x);
int delwin(WINDOW *win);
int werase(WINDOW *win);
int wnoutrefresh(WINDOW *win);
int wrefresh(WINDOW *win);
int wattron(WINDOW *win, int attrs);
int wattroff(WINDOW *win, int attrs);
int mvwprintw(WINDOW *win, int y, int x, const char *fmt, ...);
int mvwaddch(WINDOW *win, int y, int x, int ch);
int box(WINDOW *win, int vertical, int horizontal);
void wasm_curses_update_screen_size(void);

#ifdef __cplusplus
}
#endif

#define getmaxyx(win, y, x) do { \
    if ((win) == stdscr) { \
        wasm_curses_update_screen_size(); \
    } \
    (y) = (win)->height; \
    (x) = (win)->width; \
} while (0)

#ifdef WASM_CURSES_IMPLEMENTATION

#include <emscripten.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int wasm_pair_fg[128];
static int wasm_pair_bg[128];
static int wasm_keys[256];
static MEVENT wasm_mouse_events[256];
static unsigned wasm_key_read, wasm_key_write;
static MEVENT wasm_last_mouse;
static mmask_t wasm_mouse_mask;

static void wasm_queue_key(int key, MEVENT mouse) {
    unsigned next = (wasm_key_write + 1) % 256;
    if (next == wasm_key_read) wasm_key_read = (wasm_key_read + 1) % 256;
    wasm_keys[wasm_key_write] = key;
    wasm_mouse_events[wasm_key_write] = mouse;
    wasm_key_write = next;
}

EMSCRIPTEN_KEEPALIVE void curses_push_key(int key) {
    wasm_queue_key(key, (MEVENT){0});
}
EMSCRIPTEN_KEEPALIVE void curses_push_mouse(int x, int y, int wheel) {
    MEVENT event = {0};
    event.x = x; event.y = y;
    event.bstate = wheel < 0 ? BUTTON4_PRESSED : wheel > 0 ? BUTTON5_PRESSED : BUTTON1_CLICKED;
    if (event.bstate & wasm_mouse_mask) wasm_queue_key(KEY_MOUSE, event);
}
EMSCRIPTEN_KEEPALIVE void curses_push_resize(void) {
    wasm_curses_update_screen_size();
    wasm_queue_key(KEY_RESIZE, (MEVENT){0});
}

EM_JS(int, wasm_screen_cols, (), {
    return Module.term ? Module.term.cols : 80;
});

EM_JS(int, wasm_screen_rows, (), {
    return Module.term ? Module.term.rows : 24;
});

EM_JS(void, wasm_write_text, (const char *text), {
    if (Module.term) Module.term.write(UTF8ToString(text));
});

EM_JS(void, wasm_application_exited, (int status), {
    if (!Module.term) return;
    Module.term.write("\x1b[0m\x1b[40m\x1b[2J\x1b[HExited with status " + status + "\r\n");
});

WINDOW wasm_stdscr = { 24, 80, 0, 0, 0, 0 };

static char *wasm_frame;
static size_t wasm_frame_length, wasm_frame_capacity;
static int wasm_ended;
static int wasm_cursor_visible;
static void wasm_flush(void) {
    if(wasm_frame_length) {
        wasm_frame[wasm_frame_length]='\0';
        wasm_write_text(wasm_frame);
        wasm_frame_length=0;
    }
}
static void wasm_puts(const char *text) {
    size_t length=strlen(text);
    if(wasm_frame_length+length+1>wasm_frame_capacity) {
        size_t capacity=wasm_frame_capacity?wasm_frame_capacity:65536;
        while(capacity<wasm_frame_length+length+1)capacity*=2;
        char *next=(char *)realloc(wasm_frame,capacity);
        if(!next) { wasm_flush(); return; }
        wasm_frame=next; wasm_frame_capacity=capacity;
    }
    memcpy(wasm_frame+wasm_frame_length,text,length);
    wasm_frame_length+=length;
}

static void wasm_move(int y, int x) {
    char sequence[32];
    snprintf(sequence, sizeof(sequence), "\033[%d;%dH", y + 1, x + 1);
    wasm_puts(sequence);
}

static void wasm_apply_style(const WINDOW *win) {
    char sequence[48];
    int pair = win ? win->color_pair : 0;
    int fg = pair > 0 && pair < 128 ? wasm_pair_fg[pair] : COLOR_WHITE;
    int bg = pair > 0 && pair < 128 ? wasm_pair_bg[pair] : COLOR_BLACK;
    if(fg>=16)
        snprintf(sequence,sizeof(sequence),"\033[%d;38;5;%d;%dm",
                 win && win->bold ? 1 : 22,fg,40+bg);
    else
        snprintf(sequence, sizeof(sequence), "\033[%d;%d;%dm",
                 win && win->bold ? 1 : 22, fg==8?90:30+fg, 40 + bg);
    wasm_puts(sequence);
}

void wasm_curses_update_screen_size(void) {
    wasm_stdscr.height = wasm_screen_rows();
    wasm_stdscr.width = wasm_screen_cols();
}

WINDOW *initscr(void) {
    wasm_ended = 0;
    wasm_cursor_visible = 0;
    wasm_curses_update_screen_size();
    wasm_puts("\033[?25l\033[2J\033[H");
    return stdscr;
}

int endwin(void) {
    if (wasm_ended) return OK;
    wasm_ended = 1;
    wasm_frame_length = 0;
    wasm_application_exited(0);
    return OK;
}
int erase(void) {
    wasm_curses_update_screen_size();
    wasm_frame_length=0;
    /* erase discards the pending initscr/curs_set escape sequence too. */
    wasm_puts(wasm_cursor_visible ? "\033[?25h" : "\033[?25l");
    wasm_puts("\033[0m\033[2J\033[H");
    return OK;
}
int refresh(void) { wasm_flush(); return OK; }
int doupdate(void) { wasm_flush(); return OK; }
int cbreak(void) { return OK; }
int noecho(void) { return OK; }
int keypad(WINDOW *win, int enabled) { (void)win; (void)enabled; return OK; }
int nodelay(WINDOW *win, int enabled) { (void)win; (void)enabled; return OK; }
void timeout(int delay) { (void)delay; }
int getch(void) {
    if (wasm_key_read == wasm_key_write) return ERR;
    int key = wasm_keys[wasm_key_read];
    wasm_last_mouse = wasm_mouse_events[wasm_key_read];
    wasm_key_read = (wasm_key_read + 1) % 256;
    return key;
}
mmask_t mousemask(mmask_t mask, mmask_t *oldmask) {
    if (oldmask) *oldmask = wasm_mouse_mask;
    wasm_mouse_mask = mask;
    return wasm_mouse_mask;
}
int getmouse(MEVENT *event) {
    if (!event) return ERR;
    *event = wasm_last_mouse;
    return OK;
}
int curs_set(int visible) {
    int previous=wasm_cursor_visible;
    wasm_cursor_visible=visible!=0;
    wasm_puts(wasm_cursor_visible ? "\033[?25h" : "\033[?25l");
    return previous;
}
int start_color(void) { return OK; }
int use_default_colors(void) { return OK; }
int init_pair(short pair, short fg, short bg) {
    if (pair > 0 && pair < 128) {
        wasm_pair_fg[pair] = fg < 0 ? COLOR_WHITE : fg;
        wasm_pair_bg[pair] = bg < 0 ? COLOR_BLACK : bg;
    }
    return OK;
}

WINDOW *newwin(int height, int width, int y, int x) {
    WINDOW *win = (WINDOW *)calloc(1, sizeof(*win));
    if (!win) return NULL;
    win->height = height;
    win->width = width;
    win->y = y;
    win->x = x;
    return win;
}
int delwin(WINDOW *win) { free(win); return OK; }
int werase(WINDOW *win) { (void)win; return OK; }
int wnoutrefresh(WINDOW *win) { (void)win; return OK; }
int wrefresh(WINDOW *win) { (void)win; return OK; }
int wattron(WINDOW *win, int attrs) {
    if (attrs & A_BOLD) win->bold = 1;
    if ((attrs & 0xff) != 0) win->color_pair = attrs & 0xff;
    return OK;
}
int wattroff(WINDOW *win, int attrs) {
    if (attrs & A_BOLD) win->bold = 0;
    if ((attrs & 0xff) != 0) win->color_pair = 0;
    return OK;
}

int mvwprintw(WINDOW *win, int y, int x, const char *fmt, ...) {
    char buffer[8192];
    va_list args;
    if (!win || y < 0 || y >= win->height || x < 0 || x >= win->width) return ERR;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    /* Window widths count terminal cells, not UTF-8 bytes. The demo uses
       box drawing and Braille glyphs, which must never be split mid-codepoint. */
    int room = win->width - x;
    int cells = 0;
    size_t end = 0;
    while (buffer[end] && cells < room) {
        unsigned char first = (unsigned char)buffer[end];
        size_t bytes = first < 0x80 ? 1 : first < 0xe0 ? 2 : first < 0xf0 ? 3 : 4;
        size_t next = end + bytes;
        if (next >= sizeof(buffer)) break;
        int valid = 1;
        for (size_t i = end + 1; i < next; ++i)
            if (((unsigned char)buffer[i] & 0xc0) != 0x80) valid = 0;
        if (!valid) next = end + 1;
        end = next;
        ++cells;
    }
    buffer[end] = '\0';
    wasm_move(win->y + y, win->x + x);
    wasm_apply_style(win);
    wasm_puts(buffer);
    return OK;
}

int mvwaddch(WINDOW *win, int y, int x, int ch) {
    char text[2] = { (char)ch, '\0' };
    return mvwprintw(win, y, x, "%s", text);
}

// Draw a single-line Unicode box using standard box-drawing characters.
int box(WINDOW *win, int vertical, int horizontal) {
    (void)vertical;
    (void)horizontal;
    if (!win || win->height < 2 || win->width < 2) return ERR;

    wasm_move(win->y, win->x);
    wasm_apply_style(win);
    wasm_puts("┌");
    for (int column = 1; column < win->width - 1; column++) wasm_puts("─");
    wasm_puts("┐");

    wasm_move(win->y + win->height - 1, win->x);
    wasm_apply_style(win);
    wasm_puts("└");
    for (int column = 1; column < win->width - 1; column++) wasm_puts("─");
    wasm_puts("┘");

    for (int row = 1; row < win->height - 1; row++) {
        wasm_move(win->y + row, win->x);
        wasm_apply_style(win);
        wasm_puts("│");
        wasm_move(win->y + row, win->x + win->width - 1);
        wasm_puts("│");
    }
    return OK;
}

#endif // WASM_CURSES_IMPLEMENTATION
