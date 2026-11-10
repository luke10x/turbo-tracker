#ifndef TT_SAMPLE_H
#define TT_SAMPLE_H

#include <stddef.h>

enum { TT_MARKERS=16, TT_SLICES=15, TT_STEPS=16,
       TT_MAX_STEPS=256, TT_CHANNELS=6,
       TT_MAX_FRAMES=8000000 };
enum { TT_SLICE_INHERIT=-1000 };
enum { TT_PATTERN_EFFECTS=3 };
enum { TT_NOTE_OFF=-2 };
enum { TT_EFFECT_NONE=0, TT_EFFECT_G='G', TT_EFFECT_I='I',
       TT_EFFECT_J='J', TT_EFFECT_K='K', TT_EFFECT_M='M',
       TT_EFFECT_N='N', TT_EFFECT_O='O', TT_EFFECT_P='P',
       TT_EFFECT_R='R', TT_EFFECT_S='S', TT_EFFECT_T='T',
       TT_EFFECT_U='U', TT_EFFECT_V='V', TT_EFFECT_W='W', TT_EFFECT_X='X',
       TT_EFFECT_Y='Y', TT_EFFECT_Z='Z',
       TT_EFFECT_STRETCH='R', TT_EFFECT_LEGACY_RETRIGGER='r' };
enum { TT_EFFECT_REGISTER_COUNT=16, TT_EFFECT_VALUE_UNSET=-1,
       TT_EFFECT_VALUE_REGISTER_PREFIX=-2,
       TT_EFFECT_VALUE_REGISTER_BASE=-3 };

static inline int tt_effect_register_index(int command) {
    if(command>='0' && command<='9')return command-'0';
    if(command>='A' && command<='F')return command-'A'+10;
    return -1;
}
static inline int tt_effect_is_register(int value) {
    return value<=TT_EFFECT_VALUE_REGISTER_BASE &&
           value>TT_EFFECT_VALUE_REGISTER_BASE-TT_EFFECT_REGISTER_COUNT;
}
static inline int tt_effect_register_value(int index) {
    return TT_EFFECT_VALUE_REGISTER_BASE-index;
}
static inline int tt_effect_value_register(int value) {
    return TT_EFFECT_VALUE_REGISTER_BASE-value;
}
static inline void tt_effect_value_text(int value,char text[3]) {
    static const char hex[]="0123456789ABCDEF";
    if(tt_effect_is_register(value)) {
        text[0]='$';text[1]=hex[tt_effect_value_register(value)];
    } else if(value==TT_EFFECT_VALUE_REGISTER_PREFIX) {
        text[0]='$';text[1]='.';
    } else if(value>=0 && value<=255) {
        text[0]=hex[value>>4];text[1]=hex[value&15];
    } else {
        text[0]=text[1]='.';
    }
    text[2]=0;
}
static inline int tt_effect_register_command(int command) {
    return tt_effect_register_index(command)>=0;
}

typedef struct { int command,value; } TtPatternEffect;

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
    int effect2,value2,effect3,value3;
} TtCell;

static inline TtPatternEffect *tt_cell_effect(TtCell *cell,int slot) {
    return slot==0?(TtPatternEffect *)&cell->effect:
           slot==1?(TtPatternEffect *)&cell->effect2:
           slot==2?(TtPatternEffect *)&cell->effect3:NULL;
}
static inline const TtPatternEffect *tt_cell_effect_const(const TtCell *cell,int slot) {
    return tt_cell_effect((TtCell *)cell,slot);
}
static inline int tt_cell_table_effect_count(const TtCell *cell) {
    int count=0;
    for(int slot=0;slot<3;++slot)
        count+=tt_cell_effect_const(cell,slot)->command==TT_EFFECT_T;
    return count;
}
static inline TtCell tt_cell_empty(void) {
    return (TtCell){-1,-1,TT_EFFECT_NONE,-1,TT_EFFECT_NONE,-1,TT_EFFECT_NONE,-1};
}
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
int tt_detect_beats(const TtWave *wave,int markers[TT_MARKERS]);
int tt_detect_tones(const TtWave *wave,const TtPhrase *phrase,
                    int notes[TT_SLICES]);
int tt_cell_range(const TtPhrase *phrase,const TtCell *cell,int frames,
                      int *start,int *end);
void tt_note_name(int note,char out[4]);

#endif
