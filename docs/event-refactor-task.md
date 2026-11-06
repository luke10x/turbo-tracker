# Task: event-based song edits and playback

## Goal

Route every change to the song through an event layer. This is a prerequisite for
undo/redo. The event refactor keeps behaviour the same; undo/redo now records
bounded document snapshots after successful edit events.

## Two event channels

1. **Song-edit events** (`TtEditEvent`)
   - Emitted by the UI for edits and document-level load operations.
   - Applied by one function, `tt_edit_apply()`, in `src/tt_edit.c`. This is the
     only code allowed to mutate the song document.
   - Handled by the UI side (the edit layer applies them, the UI reacts to the
     result: messages, widget refresh, playback stop when needed).

2. **Playback events** (`TtPlayEvent`)
   - Emitted by the UI (play, pause, stop, mode, loop, follow, order select,
     audition) or by the playback scheduler (row played, song finished).
   - Handled by the playback scheduler (`src/tt_play.c`), which owns audio
     start/stop/render and playback position. UI actions flush immediately where
     stop-before-edit ordering matters; queued row/finish events are consumed
     during frame advancement.
   - Playback events never mutate the song document.

## Document model (current state)

- `TtSong` (in `src/tt_song.h`): patterns, pattern names, order, tables, BPM,
  rows, rows per beat, ticks per row.
- `tracker.pattern`: live copy of the pattern being edited. Pattern selection
  and save sync it through edit events.
- Samples: `tracker.samples[TT_HISTORY_MAX]` (`TtStored`: wave, phrase, paths),
  plus the current sample in `tracker.wave`, `tracker.phrase`,
  `tracker.loaded_path`, `tracker.csv_path`. The current sample is synced into
  its slot on switch.
- UI-only state (cursor, focus, selection, clipboard, scroll, dialogs, messages)
  is not part of the document and does not need events.

## Mutation inventory (src/turbo_tracker.c)

Song-edit events needed:

| Area | Functions / call sites |
|------|------------------------|
| Pattern cells | `adjust_value`, `clear_field`, `tt_down`/`tt_up` (instrument), `use_m`/`use_s`, `repeat_above`, `set_effect`, `handle_key` (note/instrument/effect/value entry and delete) |
| Pattern selection | `select_transpose`, `clear_selection`, `paste_selection` |
| Table | `handle_table_key` (name, note, instrument, effect entry), `table_clear_area`, `table_paste_area`, `table_resize` |
| Pattern bank | `order_new_pattern`, `order_rename`, `empty_pattern` |
| Order | `order_add`, `order_remove`, `order_move`, `order_bpm` |
| Settings | `apply_settings` (includes clearing rows beyond new length in all patterns) |
| Samples | `manage_rename`, `manage_ref`, `manage_move` and `manage_delete` (also patch instrument numbers in all patterns), `slices_apply`, `slices_inherit`, `slices_step` |
| Markers | `adjust_value` (wave focus), `move_wave_marker_touch`, `hold_wave_marker` |
| Document load | `load_song_at`, `load_path` (adds a sample), `recover_missing` |

Playback (not song edits), handled by the scheduler:

- `start_playback`, `stop_playback`, `toggle_play`, `toggle_mode`, `toggle_loop`,
  `toggle_follow`, `start_song`, `start_pattern_loop`
- `order_panel_selected` (play position and mode)
- `audition`, `audition_cell`, `advance_audition`
- `play_row`, `play_table_ticks`, `advance_playback` (scheduler internals)

## Planned work

1. `src/tt_edit.h` / `src/tt_edit.c`
   - `TtStored` moves here from `turbo_tracker.c`.
   - `TtEditDoc`: a view over the document (song, active pattern and index,
     sample slots, current wave/phrase, counts, message buffer).
   - `TtEditEvent`: type plus tagged union for each edit above.
   - `int tt_edit_apply(TtEditDoc *, const TtEditEvent *)`: validates, applies,
     writes a message on rejection, returns success.
2. Route the pattern, pattern bank, order, settings, table, and sample edits
   through `tt_edit_apply`. Callers build the event; they do not write fields.
3. `src/tt_play.h` / `src/tt_play.c`
   - `TtPlayEvent` and a small queue.
   - Scheduler consumes queued events during frame advancement. UI actions
     flush immediately to preserve stop-before-edit and callback timing.
   - UI-initiated playback calls become emitted events.
4. Makefile: add `tt_edit.c` and `tt_play.c` to the app objects and the test
   build. Add `test_edit.c` covering each edit type (valid and rejected).
5. Update `docs/` only if behaviour or layout changes.

## Acceptance criteria

- `make native`, `make test`, `make wasm` all pass.
- `grep` finds no direct writes to `song`, `pattern`, `table`, or sample metadata
  in `turbo_tracker.c` outside `tt_edit.c` (reads are fine).
- Playback code does not write song data.
- Behaviour unchanged: same messages, same clamping, same instrument renumbering
  on sample move/delete, same settings shrink behaviour.
- Manual smoke test: edit cells, table, order, settings, samples, play and stop
  in the native TUI.

## Open questions

- `load_song_at` emits `TT_EDIT_DOCUMENT_REPLACE` after parsing and WAV loading.
- Row played and song finished are scheduler events. The scheduler updates
  playhead and follow state when consuming them.
- Pattern selection emits `TT_EDIT_PATTERN_SELECT` because it syncs the live
  pattern into the bank.

## Chunk plan (resume here if context is lost)

Each chunk must leave `make native && make test` green before the next starts.
Mark a chunk `[x]` when done.

- [x] C1. Scaffold: `src/tt_edit.h` with `TtStored` and `TtEditDoc`; `src/tt_edit.c`
      with `tt_edit_apply` returning 0 for `TT_EDIT_NONE`. Add to Makefile
      (native + wasm object lists). No call sites changed.
- [x] C2. Pattern cell events: `TT_EDIT_CELL_SET` (exact cell overwrite; the UI
      computes the new cell from its current value, same logic as before) and
      `TT_EDIT_CELLS_BULK` (clear / paste / transpose). UI helpers in
      turbo_tracker.c: `edit_doc()`, `emit_edit()`, `edit_cell()`, `edit_cells()`.
      `TtStored` now lives only in tt_edit.h. Note entry commits the event before
      `audition_cell` (which reads the live pattern).
      Route `handle_key` cell entry, `adjust_value`, `clear_field`, `tt_down/tt_up`,
      `use_m/use_s`, `repeat_above`, `select_transpose`, `clear_selection`,
      `paste_selection`.
- [x] C3. Pattern bank + order events: `TT_EDIT_PATTERN_NEW`, `PATTERN_RENAME`,
      `ORDER_INSERT/REMOVE/SWAP`, `BPM`. UI keeps its pre-checks before
      `stop_playback` so stop timing is unchanged; rejection text comes from
      `edit_message`. `empty_pattern` replaced by public `tt_edit_clear_pattern`
      (also used by test_timeline.c fixture).
- [x] C4. Settings event: `TT_EDIT_SETTINGS` (validates, shrinks rows across all
      patterns incl. live active copy). UI still validates input fields first.
- [x] C5. Table events: `TT_EDIT_TABLE_TICK_SET` (exact tick overwrite; UI edits a
      local copy and commits only when a key actually changed it),
      `TT_EDIT_TABLE_TICKS_BULK` (clear / paste over a tick+field rectangle),
      `TT_EDIT_TABLE_LENGTH` (apply-side clamp), `TT_EDIT_TABLE_NAME` (empty name
      rejected). `table_assign_field` removed; UI helper `edit_table_ticks()`.
      Length and name edits address the table by index in the event, so
      `table_select` moving the selection cannot mis-target an edit.
- [x] C6. Sample events: rename, ref note, move, delete (instrument renumber),
      markers, slice notes, add/select/recover, and document replacement.
- [x] C7. `src/test_edit.c` covering edit types, rejection, document ownership,
      and sample instrument renumbering.
- [x] C8. Playback: `src/tt_play.h/.c` queue; route UI play/stop/pause/mode/
      loop/follow/order-select/audition through it.
- [ ] C9. Final grep check (no direct song writes in turbo_tracker.c) and
      full native TUI smoke test. Mutation grep and launch/play/exit smoke pass;
      interactive checks of every edit dialog remain.
- [x] C10. Undo/redo: Edit menu and Ctrl+Z/Ctrl+Y, 32 document snapshots,
      shared WAV buffer ownership, redo invalidation after a new edit, and
      regression tests for sample deletion and document replacement.
