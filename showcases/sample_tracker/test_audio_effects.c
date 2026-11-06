#include "audio.c"
#include <assert.h>
#include <stdio.h>

static void voice(void) {
    static float pcm[1000];
    sample_audio_stop();
    audio.voice[0].pcm=pcm;
    audio.voice[0].frames=1000;
    audio.voice[0].start=100;
    audio.voice[0].end=1000;
    audio.voice[0].source_rate=48000;
    audio.voice[0].active=1;
    audio.voice[0].position=150;
    audio.voice[0].increment=audio.voice[0].base_increment=1;
    audio.glide_current[0]=1;
}
int main(void) {
    voice();sample_audio_command(0,'P',0);
    assert(sample_audio_pan(0)==0);
    sample_audio_command(0,'P',255);
    assert(sample_audio_pan(0)==1);
    sample_audio_command(0,'P',128);
    sample_audio_command(0,'N',0x4f);
    sample_audio_tick();
    assert(sample_audio_pan(0)>.5f);
    voice();sample_audio_command(0,'I',0x20);
    assert(sample_audio_gain(0)==0);
    sample_audio_tick();
    assert(sample_audio_gain(0)>0);
    float raised=sample_audio_gain(0);
    sample_audio_command(0,'O',0x10);sample_audio_tick();
    assert(sample_audio_gain(0)<raised);
    voice();sample_audio_command(0,'J',0x40);sample_audio_tick();
    assert(sample_audio_increment(0)<1);
    voice();sample_audio_command(0,'K',0x40);sample_audio_tick();
    assert(sample_audio_increment(0)>1);
    voice();assert(sample_audio_glide_to(0,72,60,0x40));
    sample_audio_tick();assert(sample_audio_increment(0)>1);
    for(int tick=0;tick<20;++tick)sample_audio_tick();
    assert(sample_audio_increment(0)>1.99 && sample_audio_increment(0)<=2);
    voice();sample_audio_command(0,'V',0x4f);sample_audio_tick();
    assert(sample_audio_increment(0)!=1);
    double first_vibrato=sample_audio_increment(0);
    sample_audio_command(0,'V',0x4f);sample_audio_tick();
    assert(sample_audio_increment(0)!=first_vibrato);
    for(int tick=2;tick<16;++tick)sample_audio_tick();
    assert(fabs(sample_audio_increment(0)-1)<1e-10);
    voice();sample_audio_command(0,'W',0x4f);
    for(int tick=0;tick<9;++tick)sample_audio_tick();
    assert(sample_audio_gain(0)<1);
    voice();sample_audio_command(0,'r',2);
    sample_audio_tick();assert(sample_audio_position(0)==150);
    sample_audio_command(0,'r',2);
    sample_audio_tick();assert(sample_audio_position(0)==100);
    SamplePhrase phrase;sample_phrase_init(&phrase,1600);
    SampleCell reverse={60,1,SAMPLE_EFFECT_S,0xfa};
    int start,end;
    assert(sample_cell_range(&phrase,&reverse,1600,&start,&end));
    assert(start>end && start==phrase.marker[15] && end==phrase.marker[10]);
    static float data[1600];
    for(int frame=0;frame<1600;++frame)data[frame]=frame/1600.0f;
    SampleWave wave={.pcm=data,.frames=1600,.rate=48000};
    SampleVoice reversed=make_voice(&wave,start,end,60,60);
    assert(reversed.position==start-1 && reversed.increment<0 &&
           reversed.end==end);
    voice();audio.voice[0]=reversed;
    SDL_AudioSpec spec={SDL_AUDIO_F32,2,OUTPUT_RATE};
    SDL_AudioStream *stream=SDL_CreateAudioStream(&spec,&spec);
    assert(stream);
    fill_audio(NULL,stream,4*(int)(sizeof(float)*2),0);
    float rendered[8];
    assert(SDL_GetAudioStreamData(stream,rendered,sizeof(rendered))==
           (int)sizeof(rendered));
    assert(rendered[0]>rendered[2] && rendered[2]>rendered[4]);
    sample_audio_stop();
    fill_audio(NULL,stream,37*(int)(sizeof(float)*2),0);
    static float constant[1000];
    for(int frame=0;frame<1000;++frame)constant[frame]=1;
    SampleWave pulse={.pcm=constant,.frames=1000,.rate=OUTPUT_RATE};
    audio.voice[0]=make_voice(&pulse,0,1000,60,60);
    fill_audio(NULL,stream,4*(int)(sizeof(float)*2),0);
    float aligned[41*2];
    assert(SDL_GetAudioStreamData(stream,aligned,sizeof(aligned))==
           (int)sizeof(aligned));
    for(int frame=0;frame<37*2;++frame)assert(aligned[frame]==0);
    assert(aligned[37*2]>0 && aligned[37*2+1]>0);
    SDL_ClearAudioStream(stream);
    static float stretched_pcm[10000];
    for(int frame=0;frame<10000;++frame)
        stretched_pcm[frame]=sinf(2*3.14159265358979323846f*440*frame/OUTPUT_RATE);
    SampleWave stretched_wave={.pcm=stretched_pcm,.frames=10000,.rate=OUTPUT_RATE};
    sample_audio_stop();
    sample_audio_set_stretch(0,0x40);
    assert(fabs(sample_audio_stretch(0)-2)<1e-10);
    audio.voice[0]=make_voice(&stretched_wave,0,10000,60,60);
    fill_audio(NULL,stream,2400*(int)(sizeof(float)*2),0);
    assert(fabs(audio.voice[0].position-1200)<1);
    float stretched_output[2400*2];
    assert(SDL_GetAudioStreamData(stream,stretched_output,sizeof(stretched_output))==
           (int)sizeof(stretched_output));
    assert(fabsf(stretched_output[100*2]-stretched_pcm[100]*.35f)<.001f);
    sample_audio_set_stretch(0,0xc0);
    assert(fabs(sample_audio_stretch(0)-.5)<1e-10);
    audio.voice[0]=make_voice(&stretched_wave,0,10000,60,60);
    fill_audio(NULL,stream,2400*(int)(sizeof(float)*2),0);
    assert(fabs(audio.voice[0].position-4800)<1);
    SDL_DestroyAudioStream(stream);
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER,"dummy");
    assert(sample_audio_begin_song());
    assert(audio.song_mode);
    sample_audio_render_song(256);
    sample_audio_end_song();
    assert(!audio.song_mode);
    sample_audio_stop();sample_audio_close();
    puts("pitch, gain, pan, retrigger, and reverse slice effects passed");
    return 0;
}
