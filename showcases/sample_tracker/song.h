#ifndef F80_SAMPLE_TRACKER_SONG_H
#define F80_SAMPLE_TRACKER_SONG_H

#include "sample.h"

enum { SAMPLE_SONG_PATTERNS=32, SAMPLE_SONG_ORDER=128,
       SAMPLE_SONG_SAMPLES=8, SAMPLE_SONG_NAME=64,
       SAMPLE_SONG_TABLES=16, SAMPLE_TABLE_MIN_TICKS=16,
       SAMPLE_TABLE_MAX_TICKS=256, SAMPLE_TABLE_EFFECTS=6,
       SAMPLE_TICKS_PER_ROW=12, SAMPLE_MAX_TICKS_PER_ROW=48 };

typedef struct { char command; int value; } SampleTableEffect;
typedef struct {
    int note,instrument;
    SampleTableEffect effect[SAMPLE_TABLE_EFFECTS];
} SampleTableTick;
typedef struct {
    char name[SAMPLE_SONG_NAME];
    int length;
    SampleTableTick tick[SAMPLE_TABLE_MAX_TICKS];
} SampleTable;

typedef struct {
    SamplePattern pattern[SAMPLE_SONG_PATTERNS];
    char name[SAMPLE_SONG_PATTERNS][SAMPLE_SONG_NAME];
    int pattern_count;
    unsigned char order[SAMPLE_SONG_ORDER];
    int order_count,bpm,pattern_rows,rows_per_beat,ticks_per_row;
    SampleTable table[SAMPLE_SONG_TABLES];
} SampleSong;

typedef struct {
    char name[256],source_name[256],path[512];
    int frames,rate;
    SamplePhrase phrase;
} SampleSongSample;

void sample_song_init(SampleSong *song);
void sample_song_make_demo(SampleSong *song);
int sample_song_save(const char *path,const SampleSong *song,
                     const SampleSongSample *samples,int sample_count,
                     char *error,size_t error_size);
int sample_song_load(const char *path,SampleSong *song,
                     SampleSongSample *samples,int *sample_count,
                     char *error,size_t error_size);
int sample_song_resolve_path(const char *song_path,const char *saved_path,
                             char *resolved,size_t size);

#endif
