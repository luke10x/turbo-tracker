#ifndef TT_EDIT_H
#define TT_EDIT_H

#include "tt_sample.h"
#include "tt_song.h"

/* Sample slot stored in the document (moved from turbo_tracker.c). */
typedef struct {
    TtWave wave;
    TtPhrase phrase;
    char loaded_path[512],csv_path[256];
} TtStored;

/* View over the document that song-edit events mutate.
   Pointers refer to state owned by the UI; the edit layer never owns it. */
typedef struct {
    TtSong *song;
    TtPattern *active;          /* live pattern being edited */
    int *active_index;          /* index of active in song->pattern[] */
    TtStored *samples;          /* TT_HISTORY_MAX slots */
    TtWave *active_wave;        /* current sample wave */
    TtPhrase *active_phrase;    /* current sample phrase */
    int *sample_count;
    int *sample_index;
    char *active_loaded_path;
    char *active_csv_path;
    char *msg;                  /* result/rejection text */
    int msg_size;
} TtEditDoc;

typedef enum {
    TT_EDIT_NONE = 0,
    TT_EDIT_CELL_SET,           /* overwrite one active-pattern cell exactly */
    TT_EDIT_CELLS_BULK,         /* clear, paste, or transpose a rectangle */
    TT_EDIT_PATTERN_NEW,        /* append an empty, default-named pattern */
    TT_EDIT_PATTERN_RENAME,     /* rename a bank pattern */
    TT_EDIT_ORDER_INSERT,       /* insert a pattern into the play order */
    TT_EDIT_ORDER_REMOVE,       /* remove a play-order entry */
    TT_EDIT_ORDER_SWAP,         /* swap two play-order entries */
    TT_EDIT_BPM,                /* set song BPM (clamped 30..300) */
    TT_EDIT_SETTINGS,           /* set BPM, rows, rows per beat, ticks per row */
    TT_EDIT_TABLE_TICK_SET,     /* overwrite one table tick exactly */
    TT_EDIT_TABLE_TICKS_BULK,   /* clear or paste table fields over a tick range */
    TT_EDIT_TABLE_LENGTH,       /* set table length (clamped) */
    TT_EDIT_TABLE_NAME,         /* rename a table (empty names rejected) */
    TT_EDIT_SAMPLE_RENAME,
    TT_EDIT_SAMPLE_REFERENCE,
    TT_EDIT_SAMPLE_SWAP,
    TT_EDIT_SAMPLE_DELETE,
    TT_EDIT_MARKER_MOVE,
    TT_EDIT_SLICE_NOTE,
    TT_EDIT_SAMPLE_SELECT,
    TT_EDIT_SAMPLE_APPEND,
    TT_EDIT_SAMPLE_RECOVER,
    TT_EDIT_DOCUMENT_REPLACE,
    TT_EDIT_PATTERN_SELECT,
    TT_EDIT_PATTERN_SYNC,
    TT_EDIT_COUNT
} TtEditType;

typedef struct {
    int row,channel;
    TtCell cell;
} TtEditCellSet;

typedef enum {
    TT_BULK_CLEAR,              /* empty every cell in the rectangle */
    TT_BULK_PASTE,              /* copy data[] into the rectangle */
    TT_BULK_TRANSPOSE           /* add delta to notes in the rectangle */
} TtBulkOp;

typedef struct {
    TtBulkOp op;
    int top,left,bottom,right;  /* inclusive */
    int delta;                  /* transpose */
    const TtCell *data;         /* paste source, rows x cols, row-major */
    int data_cols;
} TtEditCellsBulk;

typedef struct {
    int index;                  /* PATTERN_RENAME: bank index */
    char name[TT_SONG_NAME];
} TtEditPattern;

typedef struct {
    int position;               /* INSERT/REMOVE position; SWAP first entry */
    int other;                  /* SWAP second entry */
    int pattern;                /* INSERT pattern index */
} TtEditOrder;

typedef struct {
    int bpm,pattern_rows,rows_per_beat,ticks_per_row;
} TtEditSettings;

typedef struct {
    int table,tick;
    TtTableTick value;
} TtEditTableTickSet;

/* Fields are table grid columns: 0 note, 1 instrument, 2.. effect slots. */
typedef struct {
    TtBulkOp op;                /* TT_BULK_CLEAR or TT_BULK_PASTE */
    int table;
    int top,left,bottom,right;  /* inclusive tick rows and field columns */
    const TtTableTick *data;    /* PASTE: data[row-top] supplies each tick */
} TtEditTableTicksBulk;

typedef struct {
    int table;
    int length;                 /* TABLE_LENGTH */
    char name[TT_SONG_NAME];    /* TABLE_NAME */
} TtEditTable;

typedef struct {
    int index,other,value;
    char name[256];
} TtEditSample;

typedef struct {
    int marker,frame;
} TtEditMarker;

typedef struct {
    int slice,note;
} TtEditSlice;

typedef struct {
    int index;
    TtWave *wave;              /* ownership transfers on success */
    const char *path;
} TtEditLoadedWave;

typedef struct {
    const TtSong *song;
    TtStored *samples;         /* ownership transfers on success */
    int count;
} TtEditDocumentReplace;

typedef struct {
    TtEditType type;
    union {
        TtEditCellSet cell_set;
        TtEditCellsBulk cells;
        TtEditPattern pattern;
        TtEditOrder order;
        TtEditSettings settings;
        int bpm;
        TtEditTableTickSet table_tick;
        TtEditTableTicksBulk table_ticks;
        TtEditTable table;
        TtEditSample sample;
        TtEditMarker marker;
        TtEditSlice slice;
        TtEditLoadedWave loaded_wave;
        TtEditDocumentReplace replacement;
    };
} TtEditEvent;

/* Apply one song-edit event. Returns 1 when applied, 0 when rejected
   (doc->msg then holds the reason). */
int tt_edit_apply(TtEditDoc *doc,const TtEditEvent *event);

/* Reset every cell of a pattern to empty (used to build fresh documents). */
void tt_edit_clear_pattern(TtPattern *pattern);

#endif
