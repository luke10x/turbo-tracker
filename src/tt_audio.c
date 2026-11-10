#include "tt_audio.h"
#include <SDL3/SDL.h>
#include <math.h>

enum { OUTPUT_RATE=TT_AUDIO_OUTPUT_RATE };
enum { STRETCH_HOP=1024 };
typedef struct {
    const float *pcm;
    int frames,start,end,source_rate,active;
    double position,increment,base_increment;
    double grain_start,grain_previous;
    int grain_phase,grain_index;
} TtVoice;
typedef struct {
    SDL_AudioStream *stream;
    TtVoice voice[TT_CHANNELS];
    TtVoice delayed_voice[TT_CHANNELS];
    int delayed_frames[TT_CHANNELS],delayed_active[TT_CHANNELS],delayed_kind[TT_CHANNELS];
    int delayed_note[TT_CHANNELS],delayed_reference[TT_CHANNELS],delayed_rate[TT_CHANNELS];
    float level[TT_CHANNELS];
    float volume[TT_CHANNELS];
    float pan[TT_CHANNELS],pan_offset[TT_CHANNELS],tremolo[TT_CHANNELS];
    float fade[TT_CHANNELS];
    double stretch[TT_CHANNELS];
    double pitch[TT_CHANNELS],pitch_step[TT_CHANNELS];
    double glide_target[TT_CHANNELS],glide_current[TT_CHANNELS],glide_rate[TT_CHANNELS];
    double vib_phase[TT_CHANNELS],wobble_phase[TT_CHANNELS],pan_phase[TT_CHANNELS];
    int vibrato[TT_CHANNELS],wobble[TT_CHANNELS],auto_pan[TT_CHANNELS];
    int retrigger[TT_CHANNELS],retrigger_clock[TT_CHANNELS];
    int initialized,song_mode;
} TtAudio;
static TtAudio audio={.volume={1,1,1,1,1,1},
                          .pan={.5f,.5f,.5f,.5f,.5f,.5f},
                          .tremolo={1,1,1,1,1,1},
                          .stretch={1,1,1,1,1,1}};
static TtVoice make_voice(const TtWave *wave,int start,int end,
                              int note,int reference_note) {
    double increment=(double)wave->rate/OUTPUT_RATE*pow(2.0,(note-reference_note)/12.0);
    int reverse=start>end;
    if(reverse)increment=-increment;
    double position=reverse?start-1:start;
    return (TtVoice){.pcm=wave->pcm,.frames=wave->frames,.start=start,
                         .end=end,.source_rate=wave->rate,.active=1,
                         .position=position,.increment=increment,
                         .base_increment=increment,.grain_start=position,
                         .grain_previous=position};
}
static void start_voice_now(int channel,TtVoice voice) {
    audio.voice[channel]=voice;
    audio.pitch[channel]=0;audio.glide_target[channel]=0;
    audio.glide_current[channel]=fabs(voice.increment);
    audio.retrigger_clock[channel]=0;
}
static int glide_voice_now(int channel,int note,int reference_note,int rate) {
    if(rate<=0 || !audio.voice[channel].active)return 0;
    TtVoice *voice=&audio.voice[channel];
    audio.glide_target[channel]=(double)voice->source_rate/OUTPUT_RATE*
        pow(2.0,(note-reference_note)/12.0);
    audio.pitch[channel]=0;
    if(audio.glide_current[channel]<=0)
        audio.glide_current[channel]=fabs(voice->base_increment);
    audio.glide_rate[channel]=rate/64.0;
    audio.pitch_step[channel]=0;
    return 1;
}
static float tt_at(const TtVoice *voice,double position) {
    int forward=voice->increment>=0;
    if(forward?position<voice->start || position>=voice->end:
               position>=voice->start || position<voice->end)return 0;
    int index=forward?(int)position:(int)ceil(position);
    if(index<0 || index>=voice->frames)return 0;
    int next=forward?(index+1<voice->end?index+1:index):
                     (index>voice->end?index-1:index);
    double fraction=forward?position-index:index-position;
    return (float)(voice->pcm[index]*(1.0-fraction)+
                   voice->pcm[next]*fraction);
}
static float stretched_sample(TtVoice *voice,double stretch) {
    if(voice->grain_phase>=STRETCH_HOP) {
        voice->grain_previous=voice->grain_start;
        voice->grain_start+=STRETCH_HOP*voice->increment/stretch;
        voice->grain_phase=0;
        ++voice->grain_index;
    }
    double phase=voice->grain_phase++;
    float current=tt_at(voice,voice->grain_start+phase*voice->increment);
    if(!voice->grain_index)return current;
    float previous=tt_at(voice,voice->grain_previous+
                                  (phase+STRETCH_HOP)*voice->increment);
    float mix=(float)(phase/STRETCH_HOP);
    return previous*(1-mix)+current*mix;
}

static void SDLCALL fill_audio(void *unused,SDL_AudioStream *stream,
                               int additional_amount,int total_amount) {
    (void)unused;(void)total_amount;
    float output[512];
    while(additional_amount>0) {
        float peak[TT_CHANNELS]={0};
        int frames=additional_amount/(int)(sizeof(float)*2);
        if(frames>256)frames=256;
        if(frames<1)break;
        for(int frame=0;frame<frames;++frame) {
            float left=0,right=0;
            for(int channel=0;channel<TT_CHANNELS;++channel) {
                if(audio.delayed_active[channel]) {
                    if(audio.delayed_frames[channel]<=0) {
                        if(audio.delayed_kind[channel]==2)
                            glide_voice_now(channel,audio.delayed_note[channel],
                                audio.delayed_reference[channel],audio.delayed_rate[channel]);
                        else start_voice_now(channel,audio.delayed_voice[channel]);
                        audio.delayed_active[channel]=0;
                        audio.delayed_kind[channel]=0;
                    } else --audio.delayed_frames[channel];
                }
                TtVoice *voice=&audio.voice[channel];
                int forward=voice->increment>=0;
                if(voice->active && voice->pcm &&
                   (forward?voice->position<voice->end:voice->position>=voice->end)) {
                    double stretch=audio.stretch[channel];
                    float gain=audio.volume[channel]*audio.tremolo[channel];
                    if(gain>1)gain=1;if(gain<0)gain=0;
                    float contribution=(stretch==1?
                        tt_at(voice,voice->position):
                        stretched_sample(voice,stretch))*gain;
                    float magnitude=fabsf(contribution);
                    if(magnitude>peak[channel])peak[channel]=magnitude;
                    float pan=audio.pan[channel]+audio.pan_offset[channel];
                    if(pan<0)pan=0;if(pan>1)pan=1;
                    left+=contribution*(1-pan)*.7f;
                    right+=contribution*pan*.7f;
                    voice->position+=voice->increment/stretch;
                    if(forward?voice->position>=voice->end:voice->position<voice->end)
                        voice->active=0;
                }
            }
            if(left>1)left=1;if(left< -1)left=-1;
            if(right>1)right=1;if(right< -1)right=-1;
            output[frame*2]=left;output[frame*2+1]=right;
        }
        int bytes=frames*(int)(sizeof(float)*2);
        for(int channel=0;channel<TT_CHANNELS;++channel)
            audio.level[channel]=fmaxf(peak[channel],audio.level[channel]*.8f);
        if(!SDL_PutAudioStreamData(stream,output,bytes))break;
        additional_amount-=bytes;
    }
}
int tt_audio_open(void) {
    if(audio.stream)return 1;
    if(!audio.initialized) {
        if(!SDL_Init(SDL_INIT_AUDIO))return 0;
        audio.initialized=1;
    }
    SDL_AudioSpec spec={SDL_AUDIO_F32,2,OUTPUT_RATE};
    audio.stream=SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                                           &spec,fill_audio,NULL);
    if(!audio.stream || !SDL_ResumeAudioStreamDevice(audio.stream)) {
        tt_audio_close();return 0;
    }
    return 1;
}
int tt_audio_recover(void) {
    if(!audio.stream)return 1;
    int song_mode=audio.song_mode;
    SDL_DestroyAudioStream(audio.stream);
    audio.stream=NULL;
    audio.song_mode=0;
    SDL_AudioSpec spec={SDL_AUDIO_F32,2,OUTPUT_RATE};
    audio.stream=SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                                           &spec,fill_audio,NULL);
    if(!audio.stream)return 0;
    if(song_mode && !SDL_SetAudioStreamGetCallback(audio.stream,NULL,NULL)) {
        tt_audio_close();
        return 0;
    }
    audio.song_mode=song_mode;
    if(!SDL_ResumeAudioStreamDevice(audio.stream)) {
        tt_audio_close();
        return 0;
    }
    return 1;
}
void tt_audio_stop(void) {
    if(audio.stream)SDL_LockAudioStream(audio.stream);
    for(int channel=0;channel<TT_CHANNELS;++channel) {
        audio.voice[channel].active=0;audio.level[channel]=0;
        audio.delayed_active[channel]=0;audio.delayed_frames[channel]=0;
        audio.delayed_kind[channel]=0;
        audio.volume[channel]=audio.tremolo[channel]=1;
        audio.pan[channel]=.5f;audio.pan_offset[channel]=audio.fade[channel]=0;
        audio.stretch[channel]=1;
        audio.pitch[channel]=audio.pitch_step[channel]=0;
        audio.glide_target[channel]=audio.glide_current[channel]=0;
        audio.vib_phase[channel]=audio.wobble_phase[channel]=audio.pan_phase[channel]=0;
        audio.vibrato[channel]=audio.wobble[channel]=audio.auto_pan[channel]=0;
        audio.retrigger[channel]=audio.retrigger_clock[channel]=0;
    }
    if(audio.stream) {
        SDL_ClearAudioStream(audio.stream);
        SDL_UnlockAudioStream(audio.stream);
    }
    if(audio.stream && audio.song_mode) {
        audio.song_mode=0;
        SDL_SetAudioStreamGetCallback(audio.stream,fill_audio,NULL);
    }
}
int tt_audio_begin_song(void) {
    if(!tt_audio_open())return 0;
    if(!SDL_SetAudioStreamGetCallback(audio.stream,NULL,NULL))return 0;
    audio.song_mode=1;
    SDL_ClearAudioStream(audio.stream);
    return 1;
}
int tt_audio_song_buffered_frames(void) {
    if(!audio.stream || !audio.song_mode)return 0;
    int queued=SDL_GetAudioStreamQueued(audio.stream);
    return queued>0?queued/(int)(sizeof(float)*2):0;
}
void tt_audio_render_song(int frames) {
    if(audio.stream && audio.song_mode && frames>0)
        fill_audio(NULL,audio.stream,frames*(int)(sizeof(float)*2),0);
}
void tt_audio_end_song(void) {
    if(audio.stream && audio.song_mode) {
        SDL_SetAudioStreamGetCallback(audio.stream,fill_audio,NULL);
        audio.song_mode=0;
    }
}
int tt_audio_play(const TtWave *wave,int start,int end,int note,int reference_note) {
    tt_audio_stop();
    return tt_audio_play_channel(0,wave,start,end,note,reference_note);
}
int tt_audio_play_channel(int channel,const TtWave *wave,int start,int end,
                              int note,int reference_note) {
    if(channel<0 || channel>=TT_CHANNELS)return 0;
    if(!wave || !wave->pcm || start<0 || start>wave->frames ||
       end<0 || end>wave->frames || end==start)return 0;
    if(!tt_audio_open())return 0;
    TtVoice next=make_voice(wave,start,end,note,reference_note);
    SDL_LockAudioStream(audio.stream);
    audio.delayed_active[channel]=0;
    audio.delayed_kind[channel]=0;
    start_voice_now(channel,next);
    SDL_UnlockAudioStream(audio.stream);
    return 1;
}
int tt_audio_play_channel_delayed(int channel,const TtWave *wave,int start,int end,
                                  int note,int reference_note,int delay_frames) {
    if(delay_frames<=0)
        return tt_audio_play_channel(channel,wave,start,end,note,reference_note);
    if(channel<0 || channel>=TT_CHANNELS || !wave || !wave->pcm || start<0 ||
       start>wave->frames || end<0 || end>wave->frames || end==start)return 0;
    if(!tt_audio_open())return 0;
    TtVoice next=make_voice(wave,start,end,note,reference_note);
    SDL_LockAudioStream(audio.stream);
    audio.delayed_voice[channel]=next;
    audio.delayed_frames[channel]=delay_frames;
    audio.delayed_kind[channel]=1;audio.delayed_active[channel]=1;
    SDL_UnlockAudioStream(audio.stream);
    return 1;
}
void tt_audio_cancel_delayed(int channel) {
    if(channel<0 || channel>=TT_CHANNELS)return;
    if(audio.stream)SDL_LockAudioStream(audio.stream);
    audio.delayed_active[channel]=0;audio.delayed_frames[channel]=0;
    audio.delayed_kind[channel]=0;
    if(audio.stream)SDL_UnlockAudioStream(audio.stream);
}
int tt_audio_glide_to_delayed(int channel,int note,int reference_note,int rate,
                              int delay_frames) {
    if(channel<0 || channel>=TT_CHANNELS || delay_frames<=0 || rate<=0)return 0;
    if(audio.stream)SDL_LockAudioStream(audio.stream);
    int active=audio.voice[channel].active;
    if(active) {
        audio.delayed_frames[channel]=delay_frames;
        audio.delayed_note[channel]=note;
        audio.delayed_reference[channel]=reference_note;
        audio.delayed_rate[channel]=rate;
        audio.delayed_kind[channel]=2;audio.delayed_active[channel]=1;
    }
    if(audio.stream)SDL_UnlockAudioStream(audio.stream);
    return active;
}
void tt_audio_note_off(int channel) {
    if(channel<0 || channel>=TT_CHANNELS)return;
    if(audio.stream)SDL_LockAudioStream(audio.stream);
    audio.voice[channel].active=0;
    audio.delayed_active[channel]=0;audio.delayed_frames[channel]=0;
    audio.delayed_kind[channel]=0;
    if(audio.stream)SDL_UnlockAudioStream(audio.stream);
}
void tt_audio_set_stretch(int channel,int value) {
    if(channel<0 || channel>=TT_CHANNELS || value<0 || value>255)return;
    double ratio=pow(2.0,(128-value)/64.0);
    if(audio.stream)SDL_LockAudioStream(audio.stream);
    audio.stretch[channel]=ratio;
    audio.retrigger[channel]=audio.retrigger_clock[channel]=0;
    TtVoice *voice=&audio.voice[channel];
    voice->grain_start=voice->grain_previous=voice->position;
    voice->grain_phase=voice->grain_index=0;
    if(audio.stream)SDL_UnlockAudioStream(audio.stream);
}
double tt_audio_stretch(int channel) {
    if(channel<0 || channel>=TT_CHANNELS)return 1;
    if(audio.stream)SDL_LockAudioStream(audio.stream);
    double ratio=audio.stretch[channel];
    if(audio.stream)SDL_UnlockAudioStream(audio.stream);
    return ratio;
}
int tt_audio_active(void) {
    if(!audio.stream)return 0;
    SDL_LockAudioStream(audio.stream);
    int active=0;
    for(int channel=0;channel<TT_CHANNELS;++channel)
        active|=audio.voice[channel].active;
    SDL_UnlockAudioStream(audio.stream);
    return active;
}
void tt_audio_set_volume(int channel,int value) {
    if(channel<0 || channel>=TT_CHANNELS)return;
    if(value<0)value=0;if(value>255)value=255;
    if(audio.stream)SDL_LockAudioStream(audio.stream);
    audio.volume[channel]=(float)value/255.0f;
    if(audio.stream)SDL_UnlockAudioStream(audio.stream);
}
void tt_audio_command(int channel,char command,int value) {
    if(channel<0 || channel>=TT_CHANNELS || value<0 || value>255)return;
    if(audio.stream)SDL_LockAudioStream(audio.stream);
    switch(command) {
    case 'R':audio.stretch[channel]=pow(2.0,(128-value)/64.0);
             audio.retrigger[channel]=audio.retrigger_clock[channel]=0;
             audio.voice[channel].grain_start=
                 audio.voice[channel].grain_previous=audio.voice[channel].position;
             audio.voice[channel].grain_phase=audio.voice[channel].grain_index=0;
             break;
    case 'M':audio.volume[channel]=value/255.0f;audio.fade[channel]=0;break;
    case 'I':if(value && audio.fade[channel]<=0)audio.volume[channel]=0;
             audio.fade[channel]=value/255.0f;break;
    case 'O':audio.fade[channel]=-value/255.0f;break;
    case 'J':audio.pitch_step[channel]=-value/64.0;audio.glide_target[channel]=0;break;
    case 'K':audio.pitch_step[channel]=value/64.0;audio.glide_target[channel]=0;break;
    case 'G':audio.glide_rate[channel]=value/64.0;audio.pitch_step[channel]=0;
             if(!value)audio.glide_target[channel]=0;break;
    case 'P':audio.pan[channel]=value/255.0f;break;
    case 'N':if(audio.auto_pan[channel]!=value)audio.pan_phase[channel]=0;
             audio.auto_pan[channel]=value;
             if(!value)audio.pan_offset[channel]=0;break;
    case 'V':if(audio.vibrato[channel]!=value)audio.vib_phase[channel]=0;
             audio.vibrato[channel]=value;break;
    case 'W':if(audio.wobble[channel]!=value)audio.wobble_phase[channel]=0;
             audio.wobble[channel]=value;
             if(!value)audio.tremolo[channel]=1;break;
    case 'r':if(audio.retrigger[channel]!=value)audio.retrigger_clock[channel]=0;
             audio.retrigger[channel]=value;break;
    default:break;
    }
    if(audio.stream)SDL_UnlockAudioStream(audio.stream);
}
int tt_audio_glide_to(int channel,int note,int reference_note,int rate) {
    if(channel<0 || channel>=TT_CHANNELS || rate<=0)return 0;
    if(audio.stream)SDL_LockAudioStream(audio.stream);
    int active=glide_voice_now(channel,note,reference_note,rate);
    if(audio.stream)SDL_UnlockAudioStream(audio.stream);
    return active;
}
void tt_audio_tick(void) {
    if(audio.stream)SDL_LockAudioStream(audio.stream);
    for(int channel=0;channel<TT_CHANNELS;++channel) {
        TtVoice *voice=&audio.voice[channel];
        float volume=audio.volume[channel]+audio.fade[channel];
        audio.volume[channel]=volume<0?0:volume>1?1:volume;
        if(audio.retrigger[channel] && voice->pcm &&
           ++audio.retrigger_clock[channel]>=audio.retrigger[channel]) {
            voice->position=voice->start>voice->end?voice->start-1:voice->start;
            voice->grain_start=voice->grain_previous=voice->position;
            voice->grain_phase=voice->grain_index=0;
            voice->active=1;audio.retrigger_clock[channel]=0;
        }
        double magnitude=fabs(voice->base_increment);
        if(audio.glide_target[channel]>0) {
            double current=audio.glide_current[channel];
            double target=audio.glide_target[channel];
            double ratio=pow(2.0,audio.glide_rate[channel]/12.0);
            magnitude=current<target?fmin(current*ratio,target):fmax(current/ratio,target);
            audio.glide_current[channel]=magnitude;
            if(magnitude==target) {
                voice->base_increment=copysign(target,voice->base_increment);
                audio.glide_target[channel]=0;
            }
        } else {
            audio.pitch[channel]+=audio.pitch_step[channel];
            if(audio.pitch[channel]>96)audio.pitch[channel]=96;
            if(audio.pitch[channel]< -96)audio.pitch[channel]=-96;
            magnitude*=pow(2.0,audio.pitch[channel]/12.0);
        }
        if(audio.vibrato[channel]) {
            int speed=audio.vibrato[channel]>>4,depth=audio.vibrato[channel]&15;
            audio.vib_phase[channel]+=speed*6.283185307179586/64.0;
            magnitude*=pow(2.0,sin(audio.vib_phase[channel])*depth/12.0/12.0);
        }
        if(voice->pcm)voice->increment=copysign(magnitude,voice->base_increment);
        if(audio.wobble[channel]) {
            int speed=audio.wobble[channel]>>4,depth=audio.wobble[channel]&15;
            audio.wobble_phase[channel]+=speed*6.283185307179586/64.0;
            audio.tremolo[channel]=1+depth/15.0f*(float)sin(audio.wobble_phase[channel]);
        }
        if(audio.auto_pan[channel]) {
            int speed=audio.auto_pan[channel]>>4,depth=audio.auto_pan[channel]&15;
            audio.pan_phase[channel]+=speed*6.283185307179586/64.0;
            audio.pan_offset[channel]=depth/30.0f*(float)sin(audio.pan_phase[channel]);
        }
    }
    if(audio.stream)SDL_UnlockAudioStream(audio.stream);
}
const float *tt_audio_voice_pcm(int channel) {
    if(channel<0 || channel>=TT_CHANNELS)return NULL;
    if(audio.stream)SDL_LockAudioStream(audio.stream);
    const float *value=audio.voice[channel].active?audio.voice[channel].pcm:NULL;
    if(audio.stream)SDL_UnlockAudioStream(audio.stream);
    return value;
}
int tt_audio_voice_active(int channel) {
    if(channel<0 || channel>=TT_CHANNELS)return 0;
    if(audio.stream)SDL_LockAudioStream(audio.stream);
    int value=audio.voice[channel].active;
    if(audio.stream)SDL_UnlockAudioStream(audio.stream);
    return value;
}
double tt_audio_position(int channel) {
    if(channel<0 || channel>=TT_CHANNELS)return 0;
    if(audio.stream)SDL_LockAudioStream(audio.stream);
    double value=audio.voice[channel].position;
    if(audio.stream)SDL_UnlockAudioStream(audio.stream);
    return value;
}
double tt_audio_increment(int channel) {
    if(channel<0 || channel>=TT_CHANNELS)return 0;
    if(audio.stream)SDL_LockAudioStream(audio.stream);
    double value=audio.voice[channel].increment;
    if(audio.stream)SDL_UnlockAudioStream(audio.stream);
    return value;
}
float tt_audio_pan(int channel) {
    if(channel<0 || channel>=TT_CHANNELS)return 0;
    if(audio.stream)SDL_LockAudioStream(audio.stream);
    float value=audio.pan[channel]+audio.pan_offset[channel];
    if(audio.stream)SDL_UnlockAudioStream(audio.stream);
    return value<0?0:value>1?1:value;
}
float tt_audio_gain(int channel) {
    if(channel<0 || channel>=TT_CHANNELS)return 0;
    if(audio.stream)SDL_LockAudioStream(audio.stream);
    float value=audio.volume[channel]*audio.tremolo[channel];
    if(audio.stream)SDL_UnlockAudioStream(audio.stream);
    return value<0?0:value>1?1:value;
}
float tt_audio_level(int channel) {
    if(!audio.stream || channel<0 || channel>=TT_CHANNELS)return 0;
    SDL_LockAudioStream(audio.stream);
    float level=audio.level[channel];
    SDL_UnlockAudioStream(audio.stream);
    return level;
}
void tt_audio_close(void) {
    if(audio.stream)SDL_DestroyAudioStream(audio.stream);
    audio.stream=NULL;
    audio.song_mode=0;
    for(int channel=0;channel<TT_CHANNELS;++channel)
        audio.voice[channel].active=audio.delayed_active[channel]=0;
    if(audio.initialized)SDL_QuitSubSystem(SDL_INIT_AUDIO);
    audio.initialized=0;
}
