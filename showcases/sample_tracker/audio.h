#ifndef F80_SAMPLE_TRACKER_AUDIO_H
#define F80_SAMPLE_TRACKER_AUDIO_H

#include "sample.h"

enum { SAMPLE_AUDIO_OUTPUT_RATE=48000 };

int sample_audio_open(void);
void sample_audio_close(void);
void sample_audio_stop(void);
int sample_audio_begin_song(void);
int sample_audio_song_buffered_frames(void);
void sample_audio_render_song(int frames);
void sample_audio_end_song(void);
int sample_audio_play(const SampleWave *wave,int start,int end,int note,int reference_note);
int sample_audio_play_channel(int channel,const SampleWave *wave,int start,int end,
                              int note,int reference_note);
void sample_audio_set_volume(int channel,int value);
void sample_audio_set_stretch(int channel,int value);
double sample_audio_stretch(int channel);
void sample_audio_command(int channel,char command,int value);
void sample_audio_tick(void);
int sample_audio_glide_to(int channel,int note,int reference_note,int rate);
double sample_audio_position(int channel);
double sample_audio_increment(int channel);
float sample_audio_pan(int channel);
float sample_audio_gain(int channel);
int sample_audio_active(void);
float sample_audio_level(int channel);

#endif
