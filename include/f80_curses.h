#ifndef F80_CURSES_H
#define F80_CURSES_H

/* Runtime adapter; widget and app code use the same curses API in both builds. */
#include <curses.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include "f80.h"

static inline int f80_curses_key(int key) {
    if (key >= KEY_F(1) && key <= KEY_F(10)) return 2000 + key - KEY_F(0);
    switch (key) {
#ifdef KEY_SLEFT
    case KEY_SLEFT: return F80_KEY_SHIFT_LEFT;
#endif
#ifdef KEY_SRIGHT
    case KEY_SRIGHT: return F80_KEY_SHIFT_RIGHT;
#endif
#ifdef KEY_SR
    case KEY_SR: return F80_KEY_SHIFT_UP;
#endif
#ifdef KEY_SF
    case KEY_SF: return F80_KEY_SHIFT_DOWN;
#endif
    case KEY_F(60): return F80_KEY_CTRL_LEFT;
    case KEY_F(61): return F80_KEY_CTRL_RIGHT;
    case KEY_F(62): return F80_KEY_SHIFT_UP;
    case KEY_F(63): return F80_KEY_SHIFT_DOWN;
    case KEY_UP: return F80_KEY_UP;
    case KEY_DOWN: return F80_KEY_DOWN;
    case KEY_LEFT: return F80_KEY_LEFT;
    case KEY_RIGHT: return F80_KEY_RIGHT;
    case KEY_HOME: return F80_KEY_HOME;
    case KEY_END: return F80_KEY_END;
    case KEY_PPAGE: return F80_KEY_PPAGE;
    case KEY_NPAGE: return F80_KEY_NPAGE;
    case KEY_BACKSPACE: return F80_KEY_BACKSPACE;
    case KEY_DC: return F80_KEY_DELETE;
    case KEY_BTAB: return F80_KEY_BTAB;
    default: return key;
    }
}

static inline int f80_curses_mouse(int *x, int *y, int *wheel) {
    MEVENT event;
#ifdef BUTTON1_PRESSED
    static int button_down;
#endif
    if (getmouse(&event) != OK) return ERR;
    *x = event.x; *y = event.y;
    *wheel = (event.bstate & BUTTON4_PRESSED) ? -1 : 0;
#ifdef BUTTON5_PRESSED
    if (event.bstate & BUTTON5_PRESSED) *wheel = 1;
#endif
#ifdef REPORT_MOUSE_POSITION
    if ((event.bstate & REPORT_MOUSE_POSITION) && button_down) *wheel = 2;
#endif
#ifdef BUTTON1_PRESSED
    if (event.bstate & BUTTON1_PRESSED) button_down = 1;
#endif
#ifdef BUTTON1_RELEASED
    if (event.bstate & BUTTON1_RELEASED) { button_down = 0; *wheel = 3; }
#endif
    return OK;
}

typedef int (*F80CursesFrame)(void *context);

#ifdef __EMSCRIPTEN__
static F80CursesFrame f80_curses_frame;
static void *f80_curses_context;
static void f80_curses_loop(void) {
    if (!f80_curses_frame(f80_curses_context)) {
        endwin();
        emscripten_cancel_main_loop();
    }
}
static inline void f80_curses_run(F80CursesFrame frame, void *context, int fps) {
    f80_curses_frame = frame;
    f80_curses_context = context;
    emscripten_set_main_loop(f80_curses_loop, fps, 1);
}
#else
static inline void f80_curses_run(F80CursesFrame frame, void *context, int fps) {
    timeout(fps > 0 ? 1000 / fps : 100);
    while (frame(context)) { }
    endwin();
}
#endif

#endif
