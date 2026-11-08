#include "tt_sample.h"
#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { PEAK_BLOCK=64 };

void tt_free(TtWave *wave) {
    if(!wave)return;
    SDL_free(wave->pcm);
    SDL_free(wave->peaks);
    memset(wave,0,sizeof(*wave));
}
static int build_peaks(TtWave *wave) {
    wave->peak_count=(wave->frames+PEAK_BLOCK-1)/PEAK_BLOCK;
    wave->peaks=(TtPeak *)SDL_malloc((size_t)wave->peak_count*sizeof(*wave->peaks));
    if(!wave->peaks)return 0;
    for(int block=0;block<wave->peak_count;++block) {
        int first=block*PEAK_BLOCK,last=first+PEAK_BLOCK;
        if(last>wave->frames)last=wave->frames;
        float low=1,high=-1;
        for(int frame=first;frame<last;++frame) {
            float value=wave->pcm[frame];
            if(value<low)low=value;
            if(value>high)high=value;
        }
        wave->peaks[block]=(TtPeak){low,high};
    }
    return 1;
}
int tt_load_wav(const char *path,TtWave *wave,char *error,size_t error_size) {
    SDL_AudioSpec source={0};
    Uint8 *raw=NULL,*converted=NULL;
    Uint32 raw_bytes=0;
    int converted_bytes=0;
    if(!path || !*path || !SDL_LoadWAV(path,&source,&raw,&raw_bytes)) {
        snprintf(error,error_size,"WAV load failed: %s",SDL_GetError());
        return 0;
    }
    if(raw_bytes>64u*1024u*1024u || source.freq<1000 || source.channels<1) {
        snprintf(error,error_size,"WAV is too large or has invalid audio settings");
        SDL_free(raw);return 0;
    }
    SDL_AudioSpec mono={SDL_AUDIO_F32,1,source.freq};
    if(!SDL_ConvertAudioSamples(&source,raw,(int)raw_bytes,&mono,
                                &converted,&converted_bytes)) {
        snprintf(error,error_size,"WAV conversion failed: %s",SDL_GetError());
        SDL_free(raw);return 0;
    }
    SDL_free(raw);
    int frames=converted_bytes/(int)sizeof(float);
    if(frames<2 || frames>TT_MAX_FRAMES) {
        snprintf(error,error_size,"WAV must contain 2 to %d mono frames",TT_MAX_FRAMES);
        SDL_free(converted);return 0;
    }
    TtWave next={0};
    next.pcm=(float *)converted;
    next.frames=frames;next.rate=source.freq;
    const char *name=strrchr(path,'/');
    const char *backslash=strrchr(path,'\\');
    if(backslash && (!name || backslash>name))name=backslash;
    snprintf(next.name,sizeof(next.name),"%s",name?name+1:path);
    snprintf(next.source_name,sizeof(next.source_name),"%s",next.name);
    if(!build_peaks(&next)) {
        snprintf(error,error_size,"Not enough memory for waveform");
        tt_free(&next);return 0;
    }
    tt_free(wave);
    *wave=next;
    return 1;
}
int tt_make_demo(TtWave *wave) {
    TtWave next={0};
    next.rate=44100;next.frames=next.rate*2;
    next.pcm=(float *)SDL_calloc((size_t)next.frames,sizeof(float));
    if(!next.pcm)return 0;
    for(int frame=0;frame<next.frames;++frame) {
        int hit=frame/(next.rate/8),within=frame%(next.rate/8);
        float time=(float)within/next.rate;
        float envelope=expf(-time*(hit%4==1?22.0f:38.0f));
        float pitch=90.0f+35.0f*(hit%4);
        float tone=sinf(6.2831853f*pitch*time);
        unsigned noise=(unsigned)frame*1664525u+1013904223u;
        float grit=((noise>>16)&255)/127.5f-1.0f;
        next.pcm[frame]=envelope*(.65f*tone+.16f*grit);
    }
    snprintf(next.name,sizeof(next.name),"Built-in 16-hit demo");
    snprintf(next.source_name,sizeof(next.source_name),"%s",next.name);
    if(!build_peaks(&next)) { tt_free(&next);return 0; }
    tt_free(wave);*wave=next;
    return 1;
}
void tt_reset_markers(TtPhrase *phrase,int frames) {
    for(int marker=0;marker<TT_MARKERS;++marker)
        phrase->marker[marker]=(int)((long long)marker*frames/(TT_MARKERS-1));
}
void tt_phrase_init(TtPhrase *phrase,int frames) {
    memset(phrase,0,sizeof(*phrase));
    phrase->reference_note=60;phrase->bpm=120;
    for(int slice=0;slice<TT_SLICES;++slice)
        phrase->slice_note_offset[slice]=TT_SLICE_INHERIT;
    tt_reset_markers(phrase,frames);
}
int tt_slice_base_note(const TtPhrase *phrase,int slice) {
    if(!phrase || slice<0 || slice>=TT_SLICES)return -1;
    int offset=phrase->slice_note_offset[slice];
    int note=phrase->reference_note+(offset==TT_SLICE_INHERIT?0:offset);
    return note<0?0:note>127?127:note;
}
void tt_slice_set_note(TtPhrase *phrase,int slice,int note) {
    if(!phrase || slice<0 || slice>=TT_SLICES)return;
    if(note<0)phrase->slice_note_offset[slice]=TT_SLICE_INHERIT;
    else if(note<=127)phrase->slice_note_offset[slice]=note-phrase->reference_note;
}
void tt_phrase_set_reference_note(TtPhrase *phrase,int note) {
    if(phrase && note>=0 && note<=127)phrase->reference_note=note;
}
void tt_pattern_init(TtPattern *pattern) {
    const int melody[16]={60,64,67,72,67,64,62,59,60,67,65,62,60,64,67,72};
    for(int row=0;row<TT_MAX_STEPS;++row)for(int channel=0;channel<TT_CHANNELS;++channel)
        pattern->cell[row][channel]=(TtCell){-1,-1,TT_EFFECT_NONE,-1,
                                              TT_EFFECT_NONE,-1,TT_EFFECT_NONE,-1};
    for(int row=0;row<TT_STEPS;++row)
        pattern->cell[row][0]=(TtCell){melody[row],1,TT_EFFECT_S,
                                           ((row/4)*4<<4)|
                                           ((row/4)*4+4>15?15:(row/4)*4+4),
                                           TT_EFFECT_NONE,-1,TT_EFFECT_NONE,-1};
}
void tt_move_marker(TtPhrase *phrase,int frames,int marker,int frame) {
    if(marker<0 || marker>=TT_MARKERS)return;
    int low=marker?phrase->marker[marker-1]:0;
    int high=marker+1<TT_MARKERS?phrase->marker[marker+1]:frames;
    if(frame<low)frame=low;
    if(frame>high)frame=high;
    phrase->marker[marker]=frame;
}
int tt_cell_range(const TtPhrase *phrase,const TtCell *cell,int frames,
                      int *start,int *end) {
    if(cell->note<0 || cell->instrument<0)return 0;
    if(cell->effect==TT_EFFECT_S) {
        if(cell->value<0 || cell->value>255)return 0;
        int first=cell->value>>4,last=cell->value&15;
        if(first==last)return 0;
        *start=phrase->marker[first];
        *end=phrase->marker[last];
    } else {
        *start=0;*end=frames;
    }
    return *end!=*start;
}
void tt_note_name(int note,char out[4]) {
    static const char *names[]={"C-","C#","D-","D#","E-","F-",
                                "F#","G-","G#","A-","A#","B-"};
    if(note<0 || note>127) { memcpy(out,"---",4);return; }
    out[0]=names[note%12][0];out[1]=names[note%12][1];
    out[2]=note<12?'-':(char)('0'+note/12-1);out[3]=0;
}
