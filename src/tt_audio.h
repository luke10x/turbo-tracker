#ifndef TT_AUDIO_H
#define TT_AUDIO_H

#include "tt_sample.h"

enum { TT_AUDIO_OUTPUT_RATE=48000 };

int tt_audio_open(void);
void tt_audio_close(void);
void tt_audio_stop(void);
int tt_audio_begin_song(void);
int tt_audio_song_buffered_frames(void);
void tt_audio_render_song(int frames);
void tt_audio_end_song(void);
int tt_audio_play(const TtWave *wave,int start,int end,int note,int reference_note);
int tt_audio_play_channel(int channel,const TtWave *wave,int start,int end,
                              int note,int reference_note);
void tt_audio_set_volume(int channel,int value);
void tt_audio_set_stretch(int channel,int value);
double tt_audio_stretch(int channel);
void tt_audio_command(int channel,char command,int value);
void tt_audio_tick(void);
int tt_audio_glide_to(int channel,int note,int reference_note,int rate);
double tt_audio_position(int channel);
double tt_audio_increment(int channel);
float tt_audio_pan(int channel);
float tt_audio_gain(int channel);
int tt_audio_active(void);
float tt_audio_level(int channel);

#endif
