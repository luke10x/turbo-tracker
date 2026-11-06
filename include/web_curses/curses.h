#ifndef TV_WEB_CURSES_DISPATCH_H
#define TV_WEB_CURSES_DISPATCH_H

/* Emscripten puts this directory first on the include path. Native builds
   omit it and resolve <curses.h> from the operating system. */
#ifdef WEB_CURSES_IMPLEMENTATION
#define WASM_CURSES_IMPLEMENTATION
#endif
#include "../wasm_curses.h"

#endif
