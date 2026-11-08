#ifndef TT_SONG_H
#define TT_SONG_H

#include "tt_sample.h"

enum { TT_SONG_PATTERNS=32, TT_SONG_ORDER=128,
       TT_SONG_SAMPLES=8, TT_SONG_NAME=64,
       TT_SONG_TABLES=16, TT_TABLE_MIN_TICKS=1,
       TT_TABLE_MAX_TICKS=16, TT_TABLE_DEFAULT_TICKS=6,
       TT_TABLE_LEGACY_MAX_TICKS=256, TT_TABLE_EFFECTS=6,
       TT_TICKS_PER_ROW=12, TT_MAX_TICKS_PER_ROW=48 };

typedef struct { char command; int value; } TtTableEffect;
typedef struct {
    int note,instrument;
    TtTableEffect effect[TT_TABLE_EFFECTS];
} TtTableTick;
typedef struct {
    char name[TT_SONG_NAME];
    int length;
    TtTableTick tick[TT_TABLE_MAX_TICKS];
} TtTable;

typedef struct {
    TtPattern pattern[TT_SONG_PATTERNS];
    char name[TT_SONG_PATTERNS][TT_SONG_NAME];
    int pattern_count;
    unsigned char order[TT_SONG_ORDER];
    unsigned char order_active[TT_SONG_ORDER];
    int order_count,bpm,pattern_rows,rows_per_beat,ticks_per_row;
    TtTable table[TT_SONG_TABLES];
} TtSong;

typedef struct {
    char name[256],source_name[256],path[512];
    int frames,rate;
    TtPhrase phrase;
} TtSongSample;

void tt_song_init(TtSong *song);
void tt_song_make_demo(TtSong *song);
int tt_song_save(const char *path,const TtSong *song,
                     const TtSongSample *samples,int sample_count,
                     char *error,size_t error_size);
int tt_song_load(const char *path,TtSong *song,
                     TtSongSample *samples,int *sample_count,
                     char *error,size_t error_size);
int tt_song_resolve_path(const char *song_path,const char *saved_path,
                             char *resolved,size_t size);

#endif
