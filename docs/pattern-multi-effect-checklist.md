# Pattern cells: three effect slots

## Data model

- [x] Add helper accessors around three effect slots while retaining the compatibility layout.
- [x] Initialize unused slots in the core pattern, song, edit, and playback construction paths as `TT_EFFECT_NONE` with value `-1`.
- [x] Update all pattern constructors, demo data, copies, and equality-sensitive tests.
- [x] Confirm edit snapshots, undo/redo, clipboard copy/cut/paste, and bulk edits retain all slots because they copy complete `TtCell` values.

## Song and pattern formats

- [x] Extend pattern-row text rendering to encode three effect/value pairs.
- [x] Make the parser accept both existing one-effect rows and the new three-effect representation.
- [x] Preserve existing `.song` files without data loss.
- [x] Add round-trip tests for one, two, and three effects, including empty middle/trailing slots.

## Playback and audio

- [x] Process effect slots in left-to-right order for each channel row.
- [x] Define and test ordering when effects conflict (for example, multiple volume, glide, table, or slice commands).
- [x] Apply slice selection before determining sample bounds and reference pitch.
- [x] Update audition playback to process all populated slots consistently with row playback.
- [x] Treat a pattern as non-empty when any effect slot is populated.

## Compact pattern grid

- [x] Generalize the fixed four-fields-per-channel layout to note, instrument, and one to three effect/value pairs; size each channel independently to its populated or edited slots.
- [x] Render the first effect pair by default.
- [x] Reveal effect pair 2 when Space is pressed on effect pair 1's value; move the cursor to pair 2's effect field.
- [x] Reveal effect pair 3 when Space is pressed on effect pair 2's value; move the cursor to pair 3's effect field.
- [x] Use the expanded layout for keyboard navigation, mouse hit testing, scrolling, cursor visibility, selection rendering, and status actions.
- [x] Keep extra effect pairs visible while editing effect fields.
- [x] When focus leaves effect fields or the pattern grid, compact slots left within every cell: slot 3 moves to slot 2 if slot 2 is empty.
- [x] Hide any trailing effect pair that is empty in every displayed pattern/order.

## Input behavior

- [x] Keep typed note entry (`C-4` / `C#4`) and Space field advancement.
- [x] Entering an effect letter sets the current effect slot; typing hexadecimal digits edits its current value slot.
- [x] Enter table notes with the same `C-4` / `C#4` typing used in pattern cells.
- [x] Delete clears only the focused note, instrument, effect/value pair, or value.
- [x] Ensure Space on a value expands the next effect pair rather than toggling playback while in the pattern grid.
- [x] Ensure visual selection prevents normal cell entry and still performs copy, cut, and paste correctly across all slots.

## Tests and verification

- [x] Add focused tests for expansion and cursor movement; compaction and slot shifting are covered by edit event tests.
- [x] Add tests for playback order and multi-effect interaction.
- [x] Add serialization compatibility and three-slot round-trip tests.
- [x] Run `make test`, `make native`, and `make wasm` (native and wasm pass; `make test` reaches the existing stale default-song count assertion in `test_timeline.c`).

## Inline table editor

- [x] Expand the selected `TBLxx` inline from the pattern grid, including when its value is selected.
- [x] Keep at most one table expanded and highlight its parent effect/value pair in purple.
- [x] Insert table ticks under the parent row in a separate spanning area; interrupt and resume pattern channel separators around it, and show compact note, instrument, and six effect/value columns with one space between columns.
- [x] Support inline table cursor movement and edits through table edit events.
- [x] Keep inline table width independent of pattern channel widths; Up/Down enters from the parent row and exits to adjacent pattern rows.
- [x] Use one song-wide table length, defaulting to 6 ticks and capped at 16; keep the existing table file records.
- [x] Remove the Tables window from the Song menu and update the default demo to show an inline table.
- [ ] Verify the inline layout and table length setting in the running native and web interfaces.
