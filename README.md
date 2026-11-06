# Sample Tracker showcase

A six-channel WAV slice tracker extracted from the experimental workspace as a
standalone project. It demonstrates one C codebase running as:

- a **TUI** — ncurses for rendering, SDL3 for input and audio
- a **web app** — Emscripten/WASM rendering into xterm.js through a minimal
  local "webcurses" shim that implements just the curses surface the F80
  widgets use. Works in desktop browsers and on mobile (touch input).
- an **itch.io ZIP** — the prepared web build packaged the way itch.io expects
  (index.html at the archive root).

The app itself: six sample channels, 16–256 rows per pattern, up to 32 named
patterns, up to 128 play-order entries, 16 markers per sample (15 slices),
16 editable effect tables, a zoomable 2×2 waveform pane, `.song` text files,
and live WAV loading in the browser. See
[`showcases/sample_tracker/README.md`](showcases/sample_tracker/README.md) for
the full feature and key reference.

## Layout

- `showcases/sample_tracker/` — the tracker showcase + its unit tests
- `include/f80.h`, `src/f80.c` — the small F80 widget layer (shared both builds)
- `include/f80_components.h`, `src/f80_components.c` — grid / wrap components
- `include/f80_file_dialog.h`, `src/f80_file_dialog.c` — native file dialogs
- `include/f80_curses.h` — curses-key to F80-key adapter, frame loop
- `include/wasm_curses.h`, `include/web_curses/curses.h` — the webcurses shim:
  a curses-compatible surface that renders into an xterm.js terminal
- `wasm/` — browser shell: `app.html`, `curses_terminal.js` (xterm.js wiring,
  keyboard/mouse/touch forwarding), vendored xterm.js files
- `default.song` + `tgndshtn-part*.wav` — the embedded demo song and samples
- `docs/sample-tracker-effect-status.md` — per-command status of the effects
- `scripts/package_itch.py` — packages the built web app for itch.io

The same C sources include `<curses.h>` in both builds: the native build
resolves the system ncurses, the WASM build resolves `include/web_curses/curses.h`
through its compiler include path. Browser JavaScript forwards input only.

## Native TUI

Dependencies: a C compiler, ncurses, SDL3, and `pkg-config`.

```sh
make f80-sample-tracker
./build/macos/bin/showcase-sample-tracker
```

Run the parser/model/playback tests:

```sh
make f80-sample-tracker-test
```

## Web / WASM

Dependencies: Emscripten, sourced from `../emsdk/emsdk_env.sh`.

```sh
make f80-sample-tracker-wasm
make serve
```

Then open:

```text
http://localhost:8001/app.html?app=showcases/sample-tracker
```

The generated site in `build/emscripten/www/` is disposable; `wasm/` holds the
authored browser files.

## itch.io

```sh
make itch-sample-tracker
```

Writes `build/emscripten/itch/showcase-sample-tracker.zip` — a standalone
archive with `index.html` at its root, ready to upload as an HTML game.

## Notes

- The browser build starts on the first click/tap (Web Audio requires a user
  gesture).
- In the browser, **File > Open song...** lets you pick a `.song` plus its WAVs
  together; **Save song...** downloads the text file. This works in the itch.io
  build too.
- The vendored xterm.js assets are committed; refresh them by replacing the
  files in `wasm/vendor/` (the build does not run npm).
