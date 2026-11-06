#ifndef TT_PLAY_H
#define TT_PLAY_H

typedef enum {
    TT_PLAY_START, TT_PLAY_STOP, TT_PLAY_TOGGLE,
    TT_PLAY_SET_MODE, TT_PLAY_TOGGLE_MODE,
    TT_PLAY_TOGGLE_LOOP, TT_PLAY_TOGGLE_FOLLOW,
    TT_PLAY_SELECT_ORDER, TT_PLAY_SET_ORDER_RANGE,
    TT_PLAY_AUDITION, TT_PLAY_AUDITION_CELL,
    TT_PLAY_ROW_PLAYED, TT_PLAY_FINISHED, TT_PLAY_DOCUMENT_RESET
} TtPlayType;

typedef struct {
    TtPlayType type;
    int a,b,c;
} TtPlayEvent;

/* Events are consumed by the playback scheduler. UI callers can flush before
   changing sample ownership, preserving stop-before-edit ordering. */
int tt_play_emit(TtPlayEvent event);
void tt_play_flush(void);
void tt_play_advance(void);
void tt_play_play_row(int row);
void tt_play_table_ticks(void);
int tt_play_next_tick_frames(int divisor);

#endif
