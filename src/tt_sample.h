#ifndef TT_SAMPLE_H
#define TT_SAMPLE_H

#include <stddef.h>

enum { TT_MARKERS=16, TT_SLICES=15, TT_STEPS=16,
       TT_MAX_STEPS=256, TT_CHANNELS=6,
       TT_MAX_FRAMES=8000000 };
enum { TT_SLICE_INHERIT=-1000 };
enum { TT_EFFECT_NONE=0, TT_EFFECT_G='G', TT_EFFECT_I='I',
       TT_EFFECT_J='J', TT_EFFECT_K='K', TT_EFFECT_M='M',
       TT_EFFECT_N='N', TT_EFFECT_O='O', TT_EFFECT_P='P',
       TT_EFFECT_R='R', TT_EFFECT_S='S', TT_EFFECT_T='T',
       TT_EFFECT_V='V', TT_EFFECT_W='W',
       TT_EFFECT_STRETCH='R', TT_EFFECT_LEGACY_RETRIGGER='r' };

typedef struct { float low,high; } TtPeak;
typedef struct {
    float *pcm;
    TtPeak *peaks;
    int frames,rate,peak_count;
    char name[256];
    char source_name[256];
} TtWave;
typedef struct {
    int marker[TT_MARKERS];
    int slice_note_offset[TT_SLICES];
    int reference_note,bpm;
} TtPhrase;
typedef struct {
    int note,instrument,effect,value;
} TtCell;
typedef struct {
    TtCell cell[TT_MAX_STEPS][TT_CHANNELS];
} TtPattern;

int tt_load_wav(const char *path,TtWave *wave,char *error,size_t error_size);
int tt_make_demo(TtWave *wave);
void tt_free(TtWave *wave);
void tt_phrase_init(TtPhrase *phrase,int frames);
void tt_pattern_init(TtPattern *pattern);
void tt_reset_markers(TtPhrase *phrase,int frames);
void tt_move_marker(TtPhrase *phrase,int frames,int marker,int frame);
int tt_slice_base_note(const TtPhrase *phrase,int slice);
void tt_slice_set_note(TtPhrase *phrase,int slice,int note);
void tt_phrase_set_reference_note(TtPhrase *phrase,int note);
int tt_cell_range(const TtPhrase *phrase,const TtCell *cell,int frames,
                      int *start,int *end);
void tt_note_name(int note,char out[4]);

#endif
