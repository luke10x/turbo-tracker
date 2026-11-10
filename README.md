# Turbo Tracker

A six-channel WAV slice tracker built on the F80 widget layer. One C codebase
runs as:

- a **TUI** — ncurses for rendering, SDL3 for input and audio
- a **web app** — Emscripten/WASM rendering into xterm.js through a minimal
  local "webcurses" shim that implements just the curses surface the F80
  widgets use. Works in desktop browsers and on mobile (touch input).
- an **itch.io ZIP** — the prepared web build packaged the way itch.io expects
  (`index.html` at the archive root).

The app has six sample channels, 16–256 rows per pattern, up to 32 named
patterns, up to 128 play-order entries, 16 markers per sample (15 slices),
16 editable effect tables, a zoomable 2×2 waveform pane, `.song` text files,
and live WAV loading in the browser.

## Layout

- `src/` — all C sources
  - `f80.c`, `f80_components.c`, `f80_file_dialog.c` — the F80 library module
    (kept in the `f80_*` namespace)
  - `turbo_tracker.c` — the application entry point and UI
  - `tt_sample.*`, `tt_pattern.*`, `tt_song.*`, `tt_audio.*`, `tt_csv.*` — the
    tracker model, audio, and file formats (the `tt_*` namespace)
  - `test_*.c` — unit tests
- `include/` — F80 headers, the curses-key adapter, and the webcurses shim
  (`include/web_curses/curses.h`)
- `samples/` — four demo WAVs (`tgndshtn-part1..4.wav`) and a C-4 sine
- `default.song` — the embedded demo song
- `wasm/` — browser shell: `app.html`, `curses_terminal.js` (xterm.js wiring,
  keyboard/mouse/touch forwarding), vendored xterm.js files
- `docs/turbo-tracker-effect-status.md` — per-command status of the effects
- `scripts/package_itch.py` — packages the built web app for itch.io

The same C sources include `<curses.h>` in both builds: the native build
resolves the system ncurses, the WASM build resolves
`include/web_curses/curses.h` through its compiler include path. Browser
JavaScript forwards input only.

## Build

### Native TUI

Dependencies: a C compiler, ncurses, SDL3, and `pkg-config`.

```sh
make native
./build/macos/bin/turbo-tracker
```

Run the parser/model/playback tests:

```sh
make test
```

### Web / WASM

Dependencies: Emscripten, sourced from `../emsdk/emsdk_env.sh`.

```sh
make wasm
make serve
```

Then open:

```text
http://localhost:8001/app.html
```

The generated site in `build/emscripten/www/` is disposable; `wasm/` holds the
authored browser files. The demo WAVs and `default.song` are embedded into the
WASM filesystem at build time.

### itch.io

```sh
make itch
```

Writes `build/emscripten/itch/turbo-tracker.zip` — a standalone archive with
`index.html` at its root, ready to upload as an HTML game.

## Notes

- The browser build starts on the first click/tap (Web Audio requires a user
  gesture).
- In the browser, **File > Open song...** lets you pick a `.song` plus its WAVs
  together; **Save song...** downloads the text file. This works in the itch.io
  build too.
- The vendored xterm.js assets are committed; refresh them by replacing the
  files in `wasm/vendor/` (the build does not run npm).

## Feature reference

**Edit > Undo** and **Edit > Redo** are also available with Ctrl+U and Ctrl+Y.
The tracker keeps up to 32 document edits in memory. Undo restores pattern,
table, order, settings, sample, marker, and slice changes; playback controls
are not part of edit history.

**View > Event console** or **Ctrl+O** hides the tracker panels and shows a
black, scrolling log of edit, playback, and status events. Playback logs
pattern starts instead of every played row. The menu remains available.
Page Up/Down, Home/End, or the mouse wheel scroll the log; Ctrl+O
returns to the panels. The context and message line above the shortcut bar
uses the same black console palette.

The startup song is [`default.song`](default.song). It loads four `tgndshtn`
WAVs, has 13 patterns and 28 play-order entries, and starts at BPM 157 with 16
rows per pattern, 8 rows per beat, and 12 ticks per row. Its custom sample
markers and slices are part of the song.

`INIT_SONG` selects a different startup song; unset, the app uses
`default.song`. `INIT_PLAY=1` starts playback automatically; playback is off
by default. For example:

```sh
make native
INIT_SONG=examples/glide/glide.song INIT_PLAY=1 build/macos/bin/turbo-tracker
```

The browser catalog passes the same settings through the app URL.

### Feature catalog

`make catalog` builds the **Effects** catalog as a self-contained website directory at
`build/emscripten/catalog/`. Upload that directory as-is; its `index.html`
opens the effect catalog. The navigation tree is in
[`catalog/navigation.xml`](catalog/navigation.xml), and each feature has one
focused song under [`examples/`](examples/README.md). `make catalog-wasm` is an
explicit alias for the same build. Effect entries use `Name (CODE)` titles and
are sorted alphabetically by code. Catalog links set `INIT_PLAY=1`; the regular
app remains silent on startup. The four demo WAVs and C-4 sine are embedded.

The tracker has six sample channels, 16 to 256 rows per pattern in steps of 16,
up to 32 named patterns, and up to 128 play-order entries. A pattern may appear
repeatedly in the order or remain unused. **Song > Settings...** edits BPM
(30–300), pattern rows (16–256), rows per beat (1–16), and initial ticks per row
(1–48). Their model defaults are 120, 16, 4, and 12. **Song > Patterns and
order...** shows the pattern bank and play order side by side on wide screens
and stacked on narrow screens. Shortening a pattern clears notes beyond its new
last row. New creates an empty pattern and selects it for editing, Rename
changes its name, Edit selects an existing pattern in the grid, Add inserts it
into the order, and Remove/Up/Down edit the order. BPM is a song property and
can be changed there. Selecting an ORD entry opens that pattern in the grid
and moves the playhead to its first row. As playback advances with Follow off,
the selected entry can differ from the `>` playhead marker.
The wheel or a touch swipe scrolls the ORD list without selecting another
entry or moving the playhead. Tab cycles through waveform, grid, and ORD;
Shift+Tab cycles in reverse. The shortcut bar changes to ORD actions when
that list has focus. Its **8 New** action appends an empty pattern and an
order entry for it, then selects the new entry. One Undo removes both.
Starting song playback uses the selected ORD entry and advances to the next
pattern after the configured row count; Loop restarts the order at its end.
With Follow on, the grid follows the pattern being played while keeping the
edit row and column separate from the playhead. A new pattern is empty and
plays silence
until it contains notes; the bank marks it `[empty]`. Song playback renders a
short audio buffer ahead and places ticks at output sample frames, so the screen
refresh rate does not determine note timing.

To repeat just one pattern, select it for editing and choose **Play > Loop
current pattern**, or choose PATTERN in the top mode control and press PLAY.
That starts at row 00 and repeats the selected pattern without advancing
through the song order. **Play > Start song** returns to the full order. The
top line has PLAY/PAUSE, STOP, FOLLOW/HOLD, mode, progress, and BPM. FOLLOW
moves the editor to each playing pattern; HOLD keeps the current edit pattern.
These are standard F80 buttons, with blank rows above and below for spacing and
their shadows. Space pauses and resumes the current playback mode. **Loop song
order** only affects full-song playback.

Each cell has a three-character note, two-digit hex instrument, three-letter
effect alias, and two-digit hex value. `C-401SLC04` is command `S` and plays
sample slot `01` from marker `0` to marker `4`. `MVL00`–`MVLFF` is command `M`
and changes the channel volume, including for a slice already playing. A note
with `RRR40` plays for about twice its normal duration while keeping its pitch;
`RRR80` is normal and `RRRC0` is about half duration. The stretch setting
persists on that channel and also works in a table effect slot. It uses short
overlapping grains, so extreme settings can change the sample's texture. A note
with `MVLxx` or no effect starts the full sample. Instrument `00` aliases the
selected sample; `01`–`08` address fixed sample slots. Unloaded slots are
silent. Older pattern and song files using `SLI` and `VOL` still load; saving
writes `SLC` and `MVL`. Older `RTG` retriggers still load, but new retriggers
should use a table note and a table jump. For example, put the note at tick `00`
and `X00` at tick `03` to replay the note every four ticks. `SFA` plays backward
from marker `F` to marker `A`.

Effect values may be literal hex bytes or register references such as `$0`.
Pattern commands `0RGxx` through `FRGxx` write `xx` to registers `0` through
`F`; in table slots, the register digit is the command and `xx` its value.
Other effects can read a register with `$x`, for example `MVL$0`. Registers are
shared across channels and pattern/table playback, start at `00` when playback
starts, and can be changed elsewhere in the sequence.

Sixteen ordered markers (`0`–`F`) define fifteen slices. Each slice may inherit
the sample reference note or override it. **Sampler > Slices...** shows the
frame range and duration of each interval and edits its base note. Changing the
sample reference note transposes all slice overrides. **Sampler > Manage...**
lists loaded samples and their original WAV names. It can rename, retune,
reorder, and delete samples. Reordering remaps instrument numbers in all
patterns; deleting clears cells that referenced the removed sample. The
waveform frame title shows the sample slot and display name.

### Editing

The grid shows cells as `C-4 01 SLC04`. Up/Down selects a row and Left/Right
moves through note, instrument, effect, and value fields and into adjacent
channels. On narrow screens, the grid scrolls by field. Type notes as `C-4` or
`C#4`; after a complete note, Space advances through instrument, effect, and
value fields (then to the next channel). Type `S` or `M` in the effect field to
select `SLC` or `MVL`. The same field accepts register digits `0`–`9`, register
letters `A`–`F`, and effect keys `G I J K M N O P R S T U V W X Y Z`; their
current status and byte meanings are in
[the effect status](docs/turbo-tracker-effect-status.md). The value stays `..`
until entered in the value field. Type `$` then a hex digit for a register
reference such as `$0`. F3/F4 change a selected value by 16, and
F5/F6 by 1. Delete clears a field. Enter auditions the selected cell. Outside
the pattern grid, Space controls playback. F9 opens the menu and F10 exits. Pattern F1/F2 select the
previous/next pattern. F3–F8 depend on the selected field: note controls
octave, semitone, order, and clear; instrument controls sample, markers,
management, load, and clear; effect controls type, MVL, SLC, repeat, and clear;
value controls steps of 16 or 1, repeat, and clear.

### Tables

**Song > Tables...** opens 16 tables, numbered `00`–`0F`. Each starts at 6
ticks and can be resized from 1 to 256 ticks, indexed `00`–`FF`. Each table has an editable
name above its unframed blue grid. Horizontal and vertical scrollbars appear
only when their content overflows; a swipe scrolls it, while a one-second hold
starts a rectangular selection. Shift+arrows extends the selection; F5 copies,
F6 cuts, F7 pastes into an equally sized selection of the same fields, and F8
clears it. A table has one note, one instrument, and six three-character effect
slots per tick, for example `C-4 01 M33 SAF ... ... U80 ...`. Left/Right selects
a field; Up/Down selects a tick. Tab switches between the table name and grid.
Use the note keyboard in the note field, hex digits for the instrument, and a
command letter or register digit followed by hex digits in an effect slot.
Type `$` then a hex digit to read a register. A new command shows `..` until
its value is entered. Delete clears a field. The table buttons switch
tables, change length, and close the editor; `[`/`]` and `-`/`+` do the same
from the keyboard.

Type `T` in a pattern effect field to make `TBLxx`; when that row plays, table
`xx` starts on that channel. Table notes play on that channel and need an
explicit instrument number. A cell can invoke at most one table. A later row
with a playable note or `OFF` on that channel cancels the running table and any
pending delayed note; notes and `OFF` on the row that starts the table do not.
The default is 12 table ticks per pattern row. The
six effect slots are processed left to right on each tick. Playback handles the
per-channel effects listed in the effect status, plus `U` fractional note delay, `T`
schedule the next table, `X` jump to a table, and `Y` jump to a tick in the
current table. `U` delays a note by `xx/FF` of its pattern row or table tick.
In patterns, `X` jumps to another pattern/order, `Y` jumps to a
row, and `Z` sets the runtime BPM (`xx` is hexadecimal BPM; `00` restores the
song BPM). In tables, `Z` sets ticks per row (`01`–`30` hex; `00` restores the
song setting). These runtime changes do not modify the song settings. Enter `OFF`
in the note field with `O` to stop the channel and cancel its table.
Song files save timing and pattern length in format version 6.
Versions 1–3 load with 16 rows, 4 rows per beat, and 12 ticks per row; version 1
loads empty 16-tick tables and version 2 gives tables default names. The
built-in table `00` is available for editing and effect experiments, but the
sparse default patterns do not invoke it. Tick `00` contains the example
`C-4 01 M33 SAF ... ... U05 ...`; tick `01` has vibrato, tremolo, and auto-pan,
and tick `08` has a reverse `SFA` note.

The [grid component](include/f80_components.h) also powers the catalog's
editable grid example. The same component provides keyboard navigation, touch
selection, and automatic scrollbars. The catalog's auto-scrollbar example shows
how either bar can make the other necessary. A scrollable wrapped container
lays out the table editor's buttons and has a separate catalog example.

The built-in demo starts with four sparse patterns in play order
`00 -> 02 -> 01 -> 03` so song follow mode can be tested immediately.

Shift+arrows selects whole channel cells across rows and channels. With a mouse
or finger, hold still on a grid cell for one second until it turns yellow, then
drag to extend the rectangle. A quick drag scrolls the grid vertically or
horizontally. The yellow block selects cells, not individual fields. The line
above the F-key bar shows selection and clipboard dimensions. In selection mode
F3/F4 transpose by a semitone, F5 or `y` copies, F6 or `x` cuts, F7 or `p`
pastes only into an identically sized selection, and F8 clears.
Press Ctrl+V to begin visual selection when Shift+arrow keys are unavailable;
plain arrows extend it. `y`/F5 copy, `x`/F6 cut, and `p`/F7 paste the active
selection, then end visual mode, as does Escape. Notes, effects, and values
are not entered while the selection is active.

The waveform uses F80's 2×2 drawing renderer. F1/F2 select the previous or next
sample. F3/F4 or Ctrl+Left/Right select and center the previous or next marker.
F5/F6 or Shift+Left/Right move the selected marker. F7/F8 or Down/Up zoom.
Marker and zoom actions center the selected marker as far as the sample
boundaries permit. Left/Right pans. Type `0`–`F` to select a marker. The narrow
panel beside the waveform shows play order as `00->00`: order entry `00` plays
pattern `00`. Select an entry to start song playback there; swipe the panel to
reach later entries. The grid shows neighboring ordered patterns in gray.
Tapping their rows cannot change the pattern being edited. Use the pattern keys,
status bar, or order dialog to switch the editable pattern. In song follow mode,
the playhead stays near the middle of the grid as the ordered patterns advance,
reaching the top at the start and bottom at the end. A quick horizontal drag
pans the waveform. Hold on it for one second to select the nearest marker, then
drag to move that marker between its neighbors. Tab switches waveform and
pattern focus. The active pane has a white border.

### Song files

**File > Open song...** and **Save song...** use one text `.song` file. The
versioned file lists BPM, each sample's display and original WAV names, source
path, frame count, sample reference note, marker frames and slice note offsets,
then each named pattern's sixteen rows, then the play order. Pattern rows keep
the compact six-channel format with `|` separators. WAV audio is referenced, not
embedded. Native saves WAV paths relative to the song file's directory,
including `../` for parent and sibling folders, and resolves paths from there.
The built-in demo sample is identified as `builtin:demo` and recreated on load.
A missing WAV does not block the song. Its cells stay intact but play silently;
the waveform pane turns red, shows the saved path, and offers Retry. A WAV with
the same original filename is restored automatically on upload when frame count
and rate match. A mismatched WAV still blocks loading.

In the browser, choose the `.song` and its referenced WAV files together in the
Open Song picker. The browser places selected WAVs by filename into its virtual
filesystem before the C song loader runs. The resulting `.song` uses WAV
filenames as flat relative paths. Save Song downloads the complete text file.
The browser cannot read arbitrary local paths without the user selecting the
WAVs. This also works in the itch.io ZIP build.

The older standalone slice CSV and single-pattern parsers remain in the source
for compatibility tests; the File menu now loads and saves entire songs.
