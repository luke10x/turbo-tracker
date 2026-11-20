#include "tt_audio.h"
#include "tt_song.h"
#include <string.h>
#include <SDL3/SDL.h>
#include <math.h>

enum { OUTPUT_RATE=TT_AUDIO_OUTPUT_RATE };
enum { STRETCH_HOP=1024 };
typedef struct {
    int synth,note,instrument;
    TtDx7Patch patch;
    const float *pcm;
    int frames,start,end,source_rate,active;
    double position,increment,base_increment;
    double grain_start,grain_previous;
    int grain_phase,grain_index;
} TtVoice;
typedef struct {
    SDL_AudioStream *stream;
    void *synth_state;
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
static TtAudio live_audio={.volume={1,1,1,1,1,1},
                          .pan={.5f,.5f,.5f,.5f,.5f,.5f},
                          .tremolo={1,1,1,1,1,1},
                          .stretch={1,1,1,1,1,1}};
static TtAudio offline_audio={.volume={1,1,1,1,1,1},
                              .pan={.5f,.5f,.5f,.5f,.5f,.5f},
                              .tremolo={1,1,1,1,1,1},
                              .stretch={1,1,1,1,1,1}};
static TtAudio *active_audio=&live_audio;
static int capture_active;
static TtVoice make_voice(const TtWave *wave,int start,int end,
                              int note,int reference_note) {
    if(wave->instrument_type==TT_INSTRUMENT_DX7)
        return (TtVoice){.synth=1,.patch=wave->dx7,.note=note,.active=1,
                        .source_rate=OUTPUT_RATE,.increment=1,.base_increment=1};
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
static void start_voice_now(TtAudio *audio,int channel,TtVoice voice) {
    if(voice.synth)tt_synth_context_start(audio->synth_state,channel,&voice.patch,voice.note,100);
    else tt_synth_context_stop(audio->synth_state,channel);
    audio->voice[channel]=voice;
    audio->pitch[channel]=0;audio->glide_target[channel]=0;
    audio->glide_current[channel]=fabs(voice.increment);
    audio->retrigger_clock[channel]=0;
}
static int glide_voice_now(TtAudio *audio,int channel,int note,int reference_note,int rate) {
    if(rate<=0 || !audio->voice[channel].active)return 0;
    if(audio->voice[channel].synth)reference_note=audio->voice[channel].note;
    TtVoice *voice=&audio->voice[channel];
    audio->glide_target[channel]=(double)voice->source_rate/OUTPUT_RATE*
        pow(2.0,(note-reference_note)/12.0);
    audio->pitch[channel]=0;
    if(audio->glide_current[channel]<=0)
        audio->glide_current[channel]=fabs(voice->base_increment);
    audio->glide_rate[channel]=rate/64.0;
    audio->pitch_step[channel]=0;
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
    (void)total_amount;
    TtAudio *audio=(TtAudio *)unused;
    float output[512];
    while(additional_amount>0) {
        float peak[TT_CHANNELS]={0};
        int frames=additional_amount/(int)(sizeof(float)*2);
        if(frames>256)frames=256;
        if(frames<1)break;
        for(int frame=0;frame<frames;++frame) {
            float left=0,right=0;
            for(int channel=0;channel<TT_CHANNELS;++channel) {
                if(audio->delayed_active[channel]) {
                    if(audio->delayed_frames[channel]<=0) {
                        if(audio->delayed_kind[channel]==2)
                            glide_voice_now(audio,channel,audio->delayed_note[channel],
                                audio->delayed_reference[channel],audio->delayed_rate[channel]);
                        else start_voice_now(audio,channel,audio->delayed_voice[channel]);
                        audio->delayed_active[channel]=0;
                        audio->delayed_kind[channel]=0;
                    } else --audio->delayed_frames[channel];
                }
                TtVoice *voice=&audio->voice[channel];
                int forward=voice->increment>=0;
                if(voice->synth && voice->active) {
                    float gain=audio->volume[channel]*audio->tremolo[channel];
                    float contribution=tt_synth_context_sample(audio->synth_state,channel,12*log2(fmax(1e-9,fabs(voice->increment))))*gain;
                    float pan=fmaxf(0,fminf(1,audio->pan[channel]+audio->pan_offset[channel]));
                    left+=contribution*(1-pan)*.7f;right+=contribution*pan*.7f;
                    peak[channel]=fmaxf(peak[channel],fabsf(contribution));
                    if(tt_synth_context_finished(audio->synth_state,channel))voice->active=0;
                } else if(voice->active && voice->pcm &&
                   (forward?voice->position<voice->end:voice->position>=voice->end)) {
                    double stretch=audio->stretch[channel];
                    float gain=audio->volume[channel]*audio->tremolo[channel];
                    if(gain>1)gain=1;if(gain<0)gain=0;
                    float contribution=(stretch==1?
                        tt_at(voice,voice->position):
                        stretched_sample(voice,stretch))*gain;
                    float magnitude=fabsf(contribution);
                    if(magnitude>peak[channel])peak[channel]=magnitude;
                    float pan=audio->pan[channel]+audio->pan_offset[channel];
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
            audio->level[channel]=fmaxf(peak[channel],audio->level[channel]*.8f);
        if(!SDL_PutAudioStreamData(stream,output,bytes))break;
        additional_amount-=bytes;
    }
}
int tt_audio_open(void) {
    tt_synth_init();
    if(live_audio.stream)return 1;
    if(!live_audio.initialized) {
        if(!SDL_Init(SDL_INIT_AUDIO))return 0;
        live_audio.initialized=1;
    }
    live_audio.synth_state=tt_synth_context_create();
    if(!live_audio.synth_state)return 0;
    SDL_AudioSpec spec={SDL_AUDIO_F32,2,OUTPUT_RATE};
    live_audio.stream=SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                                           &spec,fill_audio,&live_audio);
    if(!live_audio.stream || !SDL_ResumeAudioStreamDevice(live_audio.stream)) {
        tt_audio_close();return 0;
    }
    return 1;
}
int tt_audio_recover(void) {
    if(!live_audio.stream)return 1;
    int song_mode=live_audio.song_mode;
    SDL_DestroyAudioStream(live_audio.stream);
    live_audio.stream=NULL;
    live_audio.song_mode=0;
    SDL_AudioSpec spec={SDL_AUDIO_F32,2,OUTPUT_RATE};
    live_audio.stream=SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                                           &spec,fill_audio,&live_audio);
    if(!live_audio.stream)return 0;
    if(song_mode && !SDL_SetAudioStreamGetCallback(live_audio.stream,NULL,NULL)) {
        tt_audio_close();
        return 0;
    }
    live_audio.song_mode=song_mode;
    if(!SDL_ResumeAudioStreamDevice(live_audio.stream)) {
        tt_audio_close();
        return 0;
    }
    return 1;
}
void tt_audio_stop(void) {
    if(active_audio->stream)SDL_LockAudioStream(active_audio->stream);
    for(int channel=0;channel<TT_CHANNELS;++channel) {
        tt_synth_context_stop(active_audio->synth_state,channel);
        active_audio->voice[channel].active=0;active_audio->level[channel]=0;
        active_audio->delayed_active[channel]=0;active_audio->delayed_frames[channel]=0;
        active_audio->delayed_kind[channel]=0;
        active_audio->volume[channel]=active_audio->tremolo[channel]=1;
        active_audio->pan[channel]=.5f;active_audio->pan_offset[channel]=active_audio->fade[channel]=0;
        active_audio->stretch[channel]=1;
        active_audio->pitch[channel]=active_audio->pitch_step[channel]=0;
        active_audio->glide_target[channel]=active_audio->glide_current[channel]=0;
        active_audio->vib_phase[channel]=active_audio->wobble_phase[channel]=active_audio->pan_phase[channel]=0;
        active_audio->vibrato[channel]=active_audio->wobble[channel]=active_audio->auto_pan[channel]=0;
        active_audio->retrigger[channel]=active_audio->retrigger_clock[channel]=0;
    }
    if(active_audio->stream) {
        SDL_ClearAudioStream(active_audio->stream);
        SDL_UnlockAudioStream(active_audio->stream);
    }
    if(active_audio->stream && active_audio->song_mode) {
        active_audio->song_mode=0;
        SDL_SetAudioStreamGetCallback(active_audio->stream,fill_audio,active_audio);
    }
}
int tt_audio_begin_song(void) {
    if(!tt_audio_open())return 0;
    if(!SDL_SetAudioStreamGetCallback(active_audio->stream,NULL,NULL))return 0;
    active_audio->song_mode=1;
    SDL_ClearAudioStream(active_audio->stream);
    return 1;
}
int tt_audio_song_buffered_frames(void) {
    if(!active_audio->stream || !active_audio->song_mode)return 0;
    int queued=SDL_GetAudioStreamQueued(active_audio->stream);
    return queued>0?queued/(int)(sizeof(float)*2):0;
}
void tt_audio_render_song(int frames) {
    if(active_audio->stream && active_audio->song_mode && frames>0)
        fill_audio(active_audio,active_audio->stream,frames*(int)(sizeof(float)*2),0);
}
int tt_audio_capture_begin(void) {
    if(capture_active || active_audio!=&live_audio || !tt_audio_open())return 0;
    SDL_AudioSpec spec={SDL_AUDIO_F32,2,OUTPUT_RATE};
    offline_audio=(TtAudio){.stream=SDL_CreateAudioStream(&spec,&spec),
        .volume={1,1,1,1,1,1},.pan={.5f,.5f,.5f,.5f,.5f,.5f},
        .tremolo={1,1,1,1,1,1},.stretch={1,1,1,1,1,1}};
    if(!offline_audio.stream)return 0;
    offline_audio.synth_state=tt_synth_context_create();
    if(!offline_audio.synth_state) {
        SDL_DestroyAudioStream(offline_audio.stream);
        offline_audio=(TtAudio){0};return 0;
    }
    active_audio=&offline_audio;
    capture_active=1;
    return 1;
}
int tt_audio_capture_read(float *stereo,int max_frames) {
    if(!capture_active || !stereo || max_frames<=0)return 0;
    int queued=SDL_GetAudioStreamQueued(offline_audio.stream);
    int frames=queued/(int)(sizeof(float)*2);
    if(frames>max_frames)frames=max_frames;
    if(frames<=0)return 0;
    int bytes=frames*(int)(sizeof(float)*2);
    int received=SDL_GetAudioStreamData(offline_audio.stream,stereo,bytes);
    return received>0?received/(int)(sizeof(float)*2):0;
}
void tt_audio_capture_end(void) {
    if(!capture_active)return;
    SDL_DestroyAudioStream(offline_audio.stream);
    tt_synth_context_destroy(offline_audio.synth_state);
    offline_audio=(TtAudio){.volume={1,1,1,1,1,1},
        .pan={.5f,.5f,.5f,.5f,.5f,.5f},.tremolo={1,1,1,1,1,1},
        .stretch={1,1,1,1,1,1}};
    capture_active=0;
    active_audio=&live_audio;
}
void tt_audio_end_song(void) {
    if(active_audio->stream && active_audio->song_mode) {
        SDL_SetAudioStreamGetCallback(active_audio->stream,fill_audio,active_audio);
        active_audio->song_mode=0;
    }
}
int tt_audio_play(const TtWave *wave,int start,int end,int note,int reference_note) {
    tt_audio_stop();
    return tt_audio_play_channel(0,wave,start,end,note,reference_note);
}
int tt_audio_play_channel(int channel,const TtWave *wave,int start,int end,
                              int note,int reference_note) {
    return tt_audio_play_instrument_delayed(channel,-1,wave,start,end,note,reference_note,0);
}
int tt_audio_play_channel_delayed(int channel,const TtWave *wave,int start,int end,
                                  int note,int reference_note,int delay_frames) {
    return tt_audio_play_instrument_delayed(channel,-1,wave,start,end,note,reference_note,delay_frames);
}
int tt_audio_play_instrument_delayed(int channel,int instrument,const TtWave *wave,
    int start,int end,int note,int reference_note,int delay_frames) {
    if(channel<0 || channel>=TT_CHANNELS || instrument< -1 || instrument>=TT_SONG_SAMPLES || !wave ||
       (wave->instrument_type==TT_INSTRUMENT_DX7?!tt_dx7_valid(&wave->dx7):
       (!wave->pcm || start<0 || start>wave->frames || end<0 || end>wave->frames || end==start)))return 0;
    if(!tt_audio_open())return 0;
    TtVoice next=make_voice(wave,start,end,note,reference_note);next.instrument=instrument;
    SDL_LockAudioStream(active_audio->stream);
    if(delay_frames>0) {
        active_audio->delayed_voice[channel]=next;
        active_audio->delayed_frames[channel]=delay_frames;
        active_audio->delayed_kind[channel]=1;active_audio->delayed_active[channel]=1;
    } else {
        active_audio->delayed_active[channel]=0;active_audio->delayed_kind[channel]=0;
        start_voice_now(active_audio,channel,next);
    }
    SDL_UnlockAudioStream(active_audio->stream);
    return 1;
}
int tt_audio_update_dx7(int instrument,const TtDx7Patch *patch) {
    if(instrument<0 || instrument>=TT_SONG_SAMPLES || !tt_dx7_valid(patch))return 0;
    if(active_audio->stream)SDL_LockAudioStream(active_audio->stream);
    for(int c=0;c<TT_CHANNELS;++c) {
        TtVoice *v=&active_audio->voice[c];
        if(v->active && v->synth && v->instrument==instrument && memcmp(&v->patch,patch,sizeof(*patch))) {
            tt_synth_context_update(active_audio->synth_state,c,patch);v->patch=*patch;
        }
        v=&active_audio->delayed_voice[c];
        if(active_audio->delayed_active[c] && active_audio->delayed_kind[c]==1 && v->synth && v->instrument==instrument)
            v->patch=*patch;
    }
    if(active_audio->stream)SDL_UnlockAudioStream(active_audio->stream);
    return 1;
}
void tt_audio_cancel_delayed(int channel) {
    if(channel<0 || channel>=TT_CHANNELS)return;
    if(active_audio->stream)SDL_LockAudioStream(active_audio->stream);
    active_audio->delayed_active[channel]=0;active_audio->delayed_frames[channel]=0;
    active_audio->delayed_kind[channel]=0;
    if(active_audio->stream)SDL_UnlockAudioStream(active_audio->stream);
}
int tt_audio_glide_to_delayed(int channel,int note,int reference_note,int rate,
                              int delay_frames) {
    if(channel<0 || channel>=TT_CHANNELS || delay_frames<=0 || rate<=0)return 0;
    if(active_audio->stream)SDL_LockAudioStream(active_audio->stream);
    int active=active_audio->voice[channel].active;
    if(active) {
        active_audio->delayed_frames[channel]=delay_frames;
        active_audio->delayed_note[channel]=note;
        active_audio->delayed_reference[channel]=reference_note;
        active_audio->delayed_rate[channel]=rate;
        active_audio->delayed_kind[channel]=2;active_audio->delayed_active[channel]=1;
    }
    if(active_audio->stream)SDL_UnlockAudioStream(active_audio->stream);
    return active;
}
void tt_audio_note_off(int channel) {
    if(channel<0 || channel>=TT_CHANNELS)return;
    if(active_audio->stream)SDL_LockAudioStream(active_audio->stream);
    if(active_audio->voice[channel].synth)
        tt_synth_context_release(active_audio->synth_state,channel);
    else active_audio->voice[channel].active=0;
    active_audio->delayed_active[channel]=0;active_audio->delayed_frames[channel]=0;
    active_audio->delayed_kind[channel]=0;
    if(active_audio->stream)SDL_UnlockAudioStream(active_audio->stream);
}
void tt_audio_set_stretch(int channel,int value) {
    if(channel<0 || channel>=TT_CHANNELS || value<0 || value>255)return;
    double ratio=pow(2.0,(128-value)/64.0);
    if(active_audio->stream)SDL_LockAudioStream(active_audio->stream);
    active_audio->stretch[channel]=ratio;
    active_audio->retrigger[channel]=active_audio->retrigger_clock[channel]=0;
    TtVoice *voice=&active_audio->voice[channel];
    voice->grain_start=voice->grain_previous=voice->position;
    voice->grain_phase=voice->grain_index=0;
    if(active_audio->stream)SDL_UnlockAudioStream(active_audio->stream);
}
double tt_audio_stretch(int channel) {
    if(channel<0 || channel>=TT_CHANNELS)return 1;
    if(active_audio->stream)SDL_LockAudioStream(active_audio->stream);
    double ratio=active_audio->stretch[channel];
    if(active_audio->stream)SDL_UnlockAudioStream(active_audio->stream);
    return ratio;
}
int tt_audio_active(void) {
    if(!active_audio->stream)return 0;
    SDL_LockAudioStream(active_audio->stream);
    int active=0;
    for(int channel=0;channel<TT_CHANNELS;++channel)
        active|=active_audio->voice[channel].active;
    SDL_UnlockAudioStream(active_audio->stream);
    return active;
}
void tt_audio_set_volume(int channel,int value) {
    if(channel<0 || channel>=TT_CHANNELS)return;
    if(value<0)value=0;if(value>255)value=255;
    if(active_audio->stream)SDL_LockAudioStream(active_audio->stream);
    active_audio->volume[channel]=(float)value/255.0f;
    if(active_audio->stream)SDL_UnlockAudioStream(active_audio->stream);
}
void tt_audio_command(int channel,char command,int value) {
    if(channel<0 || channel>=TT_CHANNELS || value<0 || value>255)return;
    if(active_audio->stream)SDL_LockAudioStream(active_audio->stream);
    switch(command) {
    case 'R':active_audio->stretch[channel]=pow(2.0,(128-value)/64.0);
             active_audio->retrigger[channel]=active_audio->retrigger_clock[channel]=0;
             active_audio->voice[channel].grain_start=
                 active_audio->voice[channel].grain_previous=active_audio->voice[channel].position;
             active_audio->voice[channel].grain_phase=active_audio->voice[channel].grain_index=0;
             break;
    case 'M':active_audio->volume[channel]=value/255.0f;active_audio->fade[channel]=0;break;
    case 'I':if(value && active_audio->fade[channel]<=0)active_audio->volume[channel]=0;
             active_audio->fade[channel]=value/255.0f;break;
    case 'O':active_audio->fade[channel]=-value/255.0f;break;
    case 'J':active_audio->pitch_step[channel]=-value/64.0;active_audio->glide_target[channel]=0;break;
    case 'K':active_audio->pitch_step[channel]=value/64.0;active_audio->glide_target[channel]=0;break;
    case 'G':active_audio->glide_rate[channel]=value/64.0;active_audio->pitch_step[channel]=0;
             if(!value)active_audio->glide_target[channel]=0;break;
    case 'P':active_audio->pan[channel]=value/255.0f;break;
    case 'N':if(active_audio->auto_pan[channel]!=value)active_audio->pan_phase[channel]=0;
             active_audio->auto_pan[channel]=value;
             if(!value)active_audio->pan_offset[channel]=0;break;
    case 'V':if(active_audio->vibrato[channel]!=value)active_audio->vib_phase[channel]=0;
             active_audio->vibrato[channel]=value;break;
    case 'W':if(active_audio->wobble[channel]!=value)active_audio->wobble_phase[channel]=0;
             active_audio->wobble[channel]=value;
             if(!value)active_audio->tremolo[channel]=1;break;
    case 'r':if(active_audio->retrigger[channel]!=value)active_audio->retrigger_clock[channel]=0;
             active_audio->retrigger[channel]=value;break;
    default:break;
    }
    if(active_audio->stream)SDL_UnlockAudioStream(active_audio->stream);
}
int tt_audio_glide_to(int channel,int note,int reference_note,int rate) {
    if(channel<0 || channel>=TT_CHANNELS || rate<=0)return 0;
    if(active_audio->stream)SDL_LockAudioStream(active_audio->stream);
    int active=glide_voice_now(active_audio,channel,note,reference_note,rate);
    if(active_audio->stream)SDL_UnlockAudioStream(active_audio->stream);
    return active;
}
void tt_audio_tick(void) {
    if(active_audio->stream)SDL_LockAudioStream(active_audio->stream);
    for(int channel=0;channel<TT_CHANNELS;++channel) {
        TtVoice *voice=&active_audio->voice[channel];
        float volume=active_audio->volume[channel]+active_audio->fade[channel];
        active_audio->volume[channel]=volume<0?0:volume>1?1:volume;
        if(active_audio->retrigger[channel] && voice->pcm &&
           ++active_audio->retrigger_clock[channel]>=active_audio->retrigger[channel]) {
            voice->position=voice->start>voice->end?voice->start-1:voice->start;
            voice->grain_start=voice->grain_previous=voice->position;
            voice->grain_phase=voice->grain_index=0;
            voice->active=1;active_audio->retrigger_clock[channel]=0;
        }
        double magnitude=fabs(voice->base_increment);
        if(active_audio->glide_target[channel]>0) {
            double current=active_audio->glide_current[channel];
            double target=active_audio->glide_target[channel];
            double ratio=pow(2.0,active_audio->glide_rate[channel]/12.0);
            magnitude=current<target?fmin(current*ratio,target):fmax(current/ratio,target);
            active_audio->glide_current[channel]=magnitude;
            if(magnitude==target) {
                voice->base_increment=copysign(target,voice->base_increment);
                active_audio->glide_target[channel]=0;
            }
        } else {
            active_audio->pitch[channel]+=active_audio->pitch_step[channel];
            if(active_audio->pitch[channel]>96)active_audio->pitch[channel]=96;
            if(active_audio->pitch[channel]< -96)active_audio->pitch[channel]=-96;
            magnitude*=pow(2.0,active_audio->pitch[channel]/12.0);
        }
        if(active_audio->vibrato[channel]) {
            int speed=active_audio->vibrato[channel]>>4,depth=active_audio->vibrato[channel]&15;
            active_audio->vib_phase[channel]+=speed*6.283185307179586/64.0;
            magnitude*=pow(2.0,sin(active_audio->vib_phase[channel])*depth/12.0/12.0);
        }
        if(voice->pcm || voice->synth)voice->increment=copysign(magnitude,voice->base_increment);
        if(active_audio->wobble[channel]) {
            int speed=active_audio->wobble[channel]>>4,depth=active_audio->wobble[channel]&15;
            active_audio->wobble_phase[channel]+=speed*6.283185307179586/64.0;
            active_audio->tremolo[channel]=1+depth/15.0f*(float)sin(active_audio->wobble_phase[channel]);
        }
        if(active_audio->auto_pan[channel]) {
            int speed=active_audio->auto_pan[channel]>>4,depth=active_audio->auto_pan[channel]&15;
            active_audio->pan_phase[channel]+=speed*6.283185307179586/64.0;
            active_audio->pan_offset[channel]=depth/30.0f*(float)sin(active_audio->pan_phase[channel]);
        }
    }
    if(active_audio->stream)SDL_UnlockAudioStream(active_audio->stream);
}
const float *tt_audio_voice_pcm(int channel) {
    if(channel<0 || channel>=TT_CHANNELS)return NULL;
    if(active_audio->stream)SDL_LockAudioStream(active_audio->stream);
    const float *value=active_audio->voice[channel].active?active_audio->voice[channel].pcm:NULL;
    if(active_audio->stream)SDL_UnlockAudioStream(active_audio->stream);
    return value;
}
int tt_audio_voice_active(int channel) {
    if(channel<0 || channel>=TT_CHANNELS)return 0;
    if(active_audio->stream)SDL_LockAudioStream(active_audio->stream);
    int value=active_audio->voice[channel].active;
    if(active_audio->stream)SDL_UnlockAudioStream(active_audio->stream);
    return value;
}
double tt_audio_position(int channel) {
    if(channel<0 || channel>=TT_CHANNELS)return 0;
    if(active_audio->stream)SDL_LockAudioStream(active_audio->stream);
    double value=active_audio->voice[channel].position;
    if(active_audio->stream)SDL_UnlockAudioStream(active_audio->stream);
    return value;
}
double tt_audio_increment(int channel) {
    if(channel<0 || channel>=TT_CHANNELS)return 0;
    if(active_audio->stream)SDL_LockAudioStream(active_audio->stream);
    double value=active_audio->voice[channel].increment;
    if(active_audio->stream)SDL_UnlockAudioStream(active_audio->stream);
    return value;
}
float tt_audio_pan(int channel) {
    if(channel<0 || channel>=TT_CHANNELS)return 0;
    if(active_audio->stream)SDL_LockAudioStream(active_audio->stream);
    float value=active_audio->pan[channel]+active_audio->pan_offset[channel];
    if(active_audio->stream)SDL_UnlockAudioStream(active_audio->stream);
    return value<0?0:value>1?1:value;
}
float tt_audio_gain(int channel) {
    if(channel<0 || channel>=TT_CHANNELS)return 0;
    if(active_audio->stream)SDL_LockAudioStream(active_audio->stream);
    float value=active_audio->volume[channel]*active_audio->tremolo[channel];
    if(active_audio->stream)SDL_UnlockAudioStream(active_audio->stream);
    return value<0?0:value>1?1:value;
}
float tt_audio_level(int channel) {
    if(!active_audio->stream || channel<0 || channel>=TT_CHANNELS)return 0;
    SDL_LockAudioStream(active_audio->stream);
    float level=active_audio->level[channel];
    SDL_UnlockAudioStream(active_audio->stream);
    return level;
}
void tt_audio_close(void) {
    if(capture_active)tt_audio_capture_end();
    active_audio=&live_audio;
    if(live_audio.stream)SDL_DestroyAudioStream(live_audio.stream);
    live_audio.stream=NULL;
    live_audio.song_mode=0;
    for(int channel=0;channel<TT_CHANNELS;++channel)
        live_audio.voice[channel].active=live_audio.delayed_active[channel]=0;
    tt_synth_context_destroy(live_audio.synth_state);
    live_audio.synth_state=NULL;
    if(live_audio.initialized)SDL_QuitSubSystem(SDL_INIT_AUDIO);
    live_audio.initialized=0;
}
