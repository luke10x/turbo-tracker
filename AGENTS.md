# Turbo Tracker event architecture

All tracker behavior must be expressed as events. Treat this as the direction
for new work and for any existing code you modify. The current refactor has
edit and playback events, but some UI state still changes directly; migrate
those paths as you touch them.

## State ownership

- Song, pattern, order, table, sample, marker, slice, and document changes go
  through `TtEditEvent` in `src/tt_edit.h`. Apply them in `src/tt_edit.c` through
  `tt_edit_apply_recorded()` so undo and redo stay complete.
- Transport, audition, playback position, loop, mode, follow, and audio state
  go through `TtPlayEvent` in `src/tt_play.h`. Consume them in `src/tt_play.c`.
- Focus, cursor, selection, scrolling, dialogs, and other application UI state
  must also have typed events and a single handler. Add a UI event channel when
  changing these paths; do not expand the existing direct mutations in
  `src/turbo_tracker.c`.
- Platform input adapters, F80 callbacks, menu actions, and keyboard, mouse,
  and touch handlers translate input into events. They do not directly mutate
  tracker state. Playback and other internal producers also emit events for
  observable state transitions.

## Event rules

1. Define a named event type with explicit payload for each action. Do not use
   generic commands or untyped integer codes in place of a meaningful event.
2. Put validation and the resulting state change in the handler that owns that
   state. An input callback may read state to build an event, but it must not
   apply the change itself.
3. Keep event ordering explicit. Flush playback events before edits that
   replace or release sample data. Emit follow changes when manual navigation
   takes control of the grid or order list.
4. Make undo and redo record document edit events only. UI and playback events
   do not enter document history unless their payload changes the document.
5. Log useful event summaries to the event console; the console log is an
   observer, not the event transport. Avoid per-row playback log noise.
6. Add focused tests for a new event handler and its input path, especially
   rejection, ordering, and undo or redo behavior. Run `make test`,
   `make native`, and `make wasm` for cross-platform changes.

Local variables, temporary copies prepared as event payloads, initial
construction, parsing into an unattached document, and the internal state of
the reusable F80 library may be changed directly. Once state belongs to the
running tracker, change it through its owning event handler.
