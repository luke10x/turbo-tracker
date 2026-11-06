#ifndef F80_SAMPLE_TRACKER_SAMPLE_H
#define F80_SAMPLE_TRACKER_SAMPLE_H

#include <stddef.h>

enum { SAMPLE_MARKERS=16, SAMPLE_SLICES=15, SAMPLE_STEPS=16,
       SAMPLE_MAX_STEPS=256, SAMPLE_CHANNELS=6,
       SAMPLE_MAX_FRAMES=8000000 };
enum { SAMPLE_SLICE_INHERIT=-1000 };
enum { SAMPLE_EFFECT_NONE=0, SAMPLE_EFFECT_G='G', SAMPLE_EFFECT_I='I',
       SAMPLE_EFFECT_J='J', SAMPLE_EFFECT_K='K', SAMPLE_EFFECT_M='M',
       SAMPLE_EFFECT_N='N', SAMPLE_EFFECT_O='O', SAMPLE_EFFECT_P='P',
       SAMPLE_EFFECT_R='R', SAMPLE_EFFECT_S='S', SAMPLE_EFFECT_T='T',
       SAMPLE_EFFECT_V='V', SAMPLE_EFFECT_W='W',
       SAMPLE_EFFECT_STRETCH='R', SAMPLE_EFFECT_LEGACY_RETRIGGER='r' };

typedef struct { float low,high; } SamplePeak;
typedef struct {
    float *pcm;
    SamplePeak *peaks;
    int frames,rate,peak_count;
    char name[256];
    char source_name[256];
} SampleWave;
typedef struct {
    int marker[SAMPLE_MARKERS];
    int slice_note_offset[SAMPLE_SLICES];
    int reference_note,bpm;
} SamplePhrase;
typedef struct {
    int note,instrument,effect,value;
} SampleCell;
typedef struct {
    SampleCell cell[SAMPLE_MAX_STEPS][SAMPLE_CHANNELS];
} SamplePattern;

int sample_load_wav(const char *path,SampleWave *wave,char *error,size_t error_size);
int sample_make_demo(SampleWave *wave);
void sample_free(SampleWave *wave);
void sample_phrase_init(SamplePhrase *phrase,int frames);
void sample_pattern_init(SamplePattern *pattern);
void sample_reset_markers(SamplePhrase *phrase,int frames);
void sample_move_marker(SamplePhrase *phrase,int frames,int marker,int frame);
int sample_slice_base_note(const SamplePhrase *phrase,int slice);
void sample_slice_set_note(SamplePhrase *phrase,int slice,int note);
void sample_phrase_set_reference_note(SamplePhrase *phrase,int note);
int sample_cell_range(const SamplePhrase *phrase,const SampleCell *cell,int frames,
                      int *start,int *end);
void sample_note_name(int note,char out[4]);

#endif
