#ifndef TT_PLAY_H
#define TT_PLAY_H
#include "tt_synth.h"
#include "tt_sample.h"

typedef enum {
    TT_PLAY_START, TT_PLAY_STOP, TT_PLAY_TOGGLE,
    TT_PLAY_SET_MODE, TT_PLAY_TOGGLE_MODE,
    TT_PLAY_TOGGLE_LOOP, TT_PLAY_TOGGLE_FOLLOW,
    TT_PLAY_SELECT_ORDER, TT_PLAY_SELECT_PATTERN, TT_PLAY_SET_ORDER_RANGE,
    TT_PLAY_AUDITION, TT_PLAY_AUDITION_CELL, TT_PLAY_PREVIEW_SAMPLE,
    TT_PLAY_CANCEL_PREVIEW,
    TT_PLAY_ROW_PLAYED, TT_PLAY_FINISHED, TT_PLAY_DOCUMENT_RESET,
    TT_PLAY_CLEAR_ORDER_RANGE, TT_PLAY_AUDIO_RECOVER,
    TT_PLAY_PATTERN_JUMP, TT_PLAY_ROW_JUMP, TT_PLAY_SET_TICKS_PER_ROW,
    TT_PLAY_SET_BPM, TT_PLAY_SET_EFFECT_REGISTER,
    TT_PLAY_DX7_PATCH_UPDATE,
    TT_PLAY_APPLY_PENDING_FLOW
} TtPlayType;

typedef struct {
    TtPlayType type;
    int a,b,c;
    struct { int instrument; TtDx7Patch patch; } dx7_patch;
    struct { int instrument; char path[1024]; } preview_sample;
} TtPlayEvent;

/* Events are consumed by the playback scheduler. UI callers can flush before
   changing sample ownership, preserving stop-before-edit ordering. */
int tt_play_emit(TtPlayEvent event);
void tt_play_flush(void);
void tt_play_advance(void);
void tt_play_play_row(int row);
int tt_play_cell_reference_note(const TtPhrase *phrase,const TtCell *cell);
void tt_play_table_ticks(void);
int tt_play_next_tick_frames(int divisor);

#endif
