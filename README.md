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
16 editable effect tables, a zoomable 2×2 waveform pane, Git-friendly
`.ttt` tracks, and live WAV loading in the browser.

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
- `default.ttt` — the embedded demo song
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
authored browser files. The demo WAVs and `default.ttt` are embedded into the
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
- In the browser, **File > Open track...** accepts `.ttt`, legacy `.song`, and
  older `.ttt.zip` packages. **Save track...** downloads one `.ttt` file with a
  readable song header followed by packed WAV and SYX assets. You can drop
  these formats onto the terminal. This works in the itch.io build too.
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

The startup song is [`default.ttt`](default.ttt). It loads four `tgndshtn`
WAVs, has 13 patterns and 28 play-order entries, and starts at BPM 157 with 16
rows per pattern, 8 rows per beat, and 12 ticks per row. Its custom sample
markers and slices are part of the song.

`INIT_SONG` selects a different startup song; unset, the app uses
`default.ttt`. `INIT_PLAY=1` starts playback automatically; playback is off
by default. For example:

```sh
make native
INIT_SONG=examples/glide/glide.ttt INIT_PLAY=1 build/macos/bin/turbo-tracker
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
repeatedly in the order or remain unused. **File > Song settings...** edits BPM
(30–300), pattern rows (16–256), rows per beat (1–16), and initial ticks per row
(1–48). Their model defaults are 120, 16, 4, and 12. The **Orders perspective**
is available in **View** or with the leader sequence `; O`. It shows the ORD applet on the
left and the pattern bank and play order in a flexible panel on the right.
Shortening a pattern clears notes beyond its new
last row. New creates an empty pattern and selects it for editing, Rename
changes its name, Edit selects an existing pattern in the grid, Add inserts it
into the order, and Remove/Up/Down edit the order. BPM is a song property and
can be changed there. Selecting an ORD entry opens that pattern in the grid
and moves the playhead to its first row. As playback advances with Follow off,
the selected entry can differ from the `>` playhead marker.
The wheel or a touch swipe scrolls the ORD list without selecting another
entry or moving the playhead. Press `;` followed by `O`, `I`, or `X` to open
Orders, open Instruments, or return to Main. The bottom **More** and **Back**
controls show these leader shortcuts. Tab no longer cycles between editing areas. The shortcut bar changes to ORD actions
when that list has focus. Its **8 New** action appends an empty pattern and an
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
the sample reference note or override it. **View > Instruments perspective**
(`; I`) expands the waveform and places sample management and slice
editing panels below it. The Slices panel shows the frame range and duration of
each interval and edits its base note. Changing the sample reference note
transposes all slice overrides. The Manage Samples panel
lists loaded samples and their original WAV names. It can rename, retune,
reorder, and delete samples. Reordering remaps instrument numbers in all
patterns; deleting clears cells that referenced the removed sample. The
waveform frame title shows the sample slot and display name.

### Editing

The grid shows cells as `C-4 01 SLC04`. Up/Down selects a row and Left/Right
moves through note, instrument, effect, and value fields and into adjacent
channels. On narrow screens, the grid scrolls by field. Type notes as `C-4` or
`C#4`; after a complete note, Tab advances through instrument, effect, and
value fields (then to the next channel), and Shift+Tab moves back. Type `S` or `M` in the effect field to
select `SLC` or `MVL`. The same field accepts register digits `0`–`9`, register
letters `A`–`F`, and effect keys `G I J K M N O P R S T U V W X Y Z`; their
current status and byte meanings are in
[the effect status](docs/turbo-tracker-effect-status.md). The value stays `..`
until entered in the value field. Type `$` then a hex digit for a register
reference such as `$0`. In grid value-edit mode, Up/Down change a note by an
octave or a byte's high hex digit by `0x10`; Left/Right change a note by one
semitone or a byte's low digit by one. Enter toggles this mode, and Esc clears
the focused sub-cell and exits it. Shift+arrow extends visual selection; Esc
clears the selection when value-edit mode is off. Outside the grid, Enter
controls playback. F9 opens the menu and F10 exits. Pattern F1/F2 select the
previous/next pattern. F3–F8 depend on the selected field: note controls
octave, semitone, order, and clear; instrument controls sample, markers,
management, load, and clear; effect controls type, MVL, SLC, repeat, and clear;
value controls steps of 16 or 1, repeat, and clear.

### Controller

The controller uses the same actions as the keyboard: **Affirmative** is Sony
Cross/Xbox A, **Negative** is Circle/B, **Focus** is Square/X, and **Option**
is Triangle/Y. The D-pad moves the cursor. Hold Affirmative in the grid and
use the D-pad or shoulder buttons to edit the focused value: Top Left/L1 and
Bottom Left/L2 make major steps; Top Right/R1 and Bottom Right/R2 make minor
steps. Outside the grid, Top Left/Top Right navigate the highlighted Play,
Stop, Follow, Song/Pattern, and Lock controls; Affirmative activates the
highlighted control. Bottom Left/Bottom Right navigate highlighted status-bar
actions; Affirmative activates one. Negative exits those modes.

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
drag to move that marker between its neighbors. Use the bottom perspective
controls to switch layouts. The active pane has a white border.

### Track files

**File > Save track...** writes one `.ttt` file. The song is a readable text
header at the start of the file; an appended ZIP holds the WAVs and any loaded
SYX banks. Paths beginning with `!samples/` refer to the assets in that same
file. The text header is authoritative when loading the track. Native builds
use the file browser; browser builds use the download bridge.

The text header lists BPM, instrument names and source paths, reference notes,
markers, slice offsets, named patterns, and play order. Pattern rows use the
compact six-channel format with `|` separators. A standalone text `.ttt` or
legacy `.song` still loads; saving it produces the packed `.ttt` format. Older
`.ttt.zip` and `.tts.zip` packages also load. The embedded
[`default.ttt`](default.ttt) remains a standalone text track.

In **Instruments > Instrument Settings > Source...**, **This track** selects
`!samples/NN.wav`; Save Track packs that WAV into the `.ttt`. **Browse...**
chooses an external WAV, and the editable path can also use `/`, `~/`, `./`, or
`../`. External WAVs remain on disk and are never copied into the `.ttt`.
Choose **This track** again to pack the currently loaded WAV on the next save.
Saving to a new native location rebases relative external references to keep
them pointing at the same file. The browser can use uploaded external WAVs in
its virtual filesystem; they must be supplied again in a new browser session.

Because the ZIP tail is binary, configure Git once in this checkout to diff only
the authoritative header: `git config diff.ttt.textconv "python3 scripts/ttt_textconv.py"`.
The tracked `.gitattributes` associates `.ttt` files with that diff driver.

A missing WAV does not block loading or saving a track. Its cells remain but
play silently; the waveform pane turns red, shows the saved path, and offers
Retry. Saving keeps its `!samples/` reference so the WAV can be restored later. Uploading a WAV with the same original filename restores it when frame
count and rate match. In the browser, choose a `.ttt` and its WAV files together
when opening a standalone track. The browser stores selected WAVs by filename
in its virtual filesystem. It cannot read arbitrary local paths without the
user choosing those files. Dropping a track or package onto the web terminal
opens it too.

The older standalone slice CSV and single-pattern parsers remain in the source
for compatibility tests; the File menu loads and saves entire tracks.

## FM synth instruments

Every channel can play either a sample or an MSFA DX7 instrument, and can switch
between them on successive notes. Channels remain monophonic. Instrument numbers
and table instrument references work for both types.

Choose **File → New FM** to add a synth without a WAV and open its editor.
The existing eight-slot bank limit applies to samples and synths together.
Sample instruments show Markers beside Sample Settings, with reference-note
controls in Sample Settings. FM instruments show FM Settings above the shared
Instrument settings panel. The Instrument list sits to the left of shared settings
and shows each instrument's type, number, and name. Undo/redo restores FM patch edits.

The DX7 editor replaces sample/marker controls with global settings and six
operator panels. Click a parameter or use arrows to select it; `+`/`-` changes
its value. Page Up/Down and the mouse wheel scroll. Enter auditions C4, Space
stops it, and the footer has clickable decrement/increment/audition/stop buttons.
Patch edits (including output level and coarse ratio), imports into an existing
DX7 instrument, and undo/redo update sounding voices and pending delayed notes
using that instrument. Envelope progress, release state and oscillator/LFO phase
are preserved. Sync switches apply at the next note-on. Updates take effect at
the next synthesis block; audio already queued to the device still has its normal
buffering latency. Operator mode supports ratio and fixed
frequency; detune is displayed as -7..+7 and transpose as -24..+24. Algorithms
use 0..31. Pitch and amplitude envelopes use four rates and four levels; the
pitch envelope is an original DX7 feature. DX7 scaling curves are 0=-linear,
1=-exponential, 2=+exponential, 3=+linear; breakpoint 0 is A-1 (MIDI 21).

**Import .syx** accepts Yamaha single-voice and 32-voice-bank dumps, validating
framing, checksum and parameter ranges before changing the instrument. Use
**Bank voice -/+** to select 1..32 before importing a bank; import replaces the
selected instrument's patch and name in one undoable edit. No external effects
or third-party patch bank is bundled.

Songs now save version 7 instrument tags and complete DX7 patches. Versions
1..6 still load as sample instruments. Synths play through the normal channel
mixer, with pan, volume, pitch modulation, glide and delayed triggers; sample
slicing/time stretching applies only to sample instruments. Note-off releases
the DX7 envelopes rather than cutting them. See `third_party/msfa/README.md` for
engine provenance and the amplitude-modulation approximation.

INIT VOICE holds a steady sine at full operator sustain until note-off. Synth
output is calibrated so a single full-level carrier peaks at 0.5 before the
channel mixer (about -15 dBFS per side when centered at full channel volume).
Imported patch output levels are preserved.

Native builds require a C++11 compiler in addition to the C toolchain.
