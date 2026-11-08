# Pattern cells: three effect slots

## Data model

- [x] Add helper accessors around three effect slots while retaining the compatibility layout.
- [x] Initialize unused slots in the core pattern, song, edit, and playback construction paths as `TT_EFFECT_NONE` with value `-1`.
- [ ] Update all pattern constructors, demo data, copies, and equality-sensitive tests.
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

- [ ] Generalize the fixed four-fields-per-channel layout to note, instrument, and one to three effect/value pairs.
- [ ] Render the first effect pair by default.
- [ ] Reveal effect pair 2 when Space is pressed on effect pair 1's value; move the cursor to pair 2's effect field.
- [ ] Reveal effect pair 3 when Space is pressed on effect pair 2's value; move the cursor to pair 3's effect field.
- [ ] Use the expanded layout for keyboard navigation, mouse hit testing, scrolling, cursor visibility, selection rendering, and status actions.
- [ ] Keep extra effect pairs visible while editing effect fields.
- [ ] When focus leaves effect fields or the pattern grid, compact slots left within every cell: slot 3 moves to slot 2 if slot 2 is empty.
- [ ] Hide any trailing effect pair that is empty in every displayed pattern/order.

## Input behavior

- [ ] Keep typed note entry (`C-4` / `C#4`) and Space field advancement.
- [ ] Entering an effect letter sets the current effect slot; typing hexadecimal digits edits its current value slot.
- [ ] Delete clears only the focused note, instrument, effect/value pair, or value.
- [ ] Ensure Space on a value expands the next effect pair rather than toggling playback while in the pattern grid.
- [ ] Ensure visual selection prevents normal cell entry and still performs copy, cut, and paste correctly across all slots.

## Tests and verification

- [ ] Add focused tests for expansion, cursor movement, compaction, and slot shifting.
- [x] Add tests for playback order and multi-effect interaction.
- [ ] Add serialization compatibility and three-slot round-trip tests.
- [ ] Run `make test`, `make native`, and `make wasm`.
