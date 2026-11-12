#include "tt_synth.h"
#include "tt_song.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
static void checksum(uint8_t *d,int n) {
    int sum=0;for(int i=6;i<n-2;++i)sum+=d[i];
    d[n-2]=(uint8_t)((-sum)&127);d[n-1]=0xf7;
}
int main(void) {
    TtDx7Patch init,p;tt_dx7_init(&init);assert(tt_dx7_valid(&init));
    uint8_t single[163]={0xf0,0x43,0,0,1,27};
    memcpy(single+6,init.data,155);checksum(single,163);
    assert(tt_dx7_import(single,sizeof(single),0,&p));
    assert(!memcmp(&init,&p,sizeof(p)));
    single[10]^=1;assert(!tt_dx7_import(single,sizeof(single),0,&p));single[10]^=1;
    single[6+134]=32;checksum(single,163);assert(!tt_dx7_import(single,163,0,&p));
    single[6+134]=0;checksum(single,163);
    assert(!tt_dx7_import(single,162,0,&p));assert(!tt_dx7_import(single,163,1,&p));
    uint8_t bank[4104]={0xf0,0x43,0,9,32,0};
    for(int v=0;v<32;++v) {
        uint8_t *b=bank+6+128*v;
        for(int op=0;op<6;++op) {
            const uint8_t *d=init.data+21*op;uint8_t *t=b+17*op;
            memcpy(t,d,11);t[11]=d[11]|d[12]<<2;t[12]=d[13]|d[20]<<3;
            t[13]=d[14]|d[15]<<2;t[14]=d[16];t[15]=d[17]|d[18]<<1;t[16]=d[19];
        }
        memcpy(b+102,init.data+126,9);b[110]=v;b[111]=init.data[135]|init.data[136]<<3;
        memcpy(b+112,init.data+137,4);b[116]=init.data[141]|init.data[142]<<1|init.data[143]<<4;
        memcpy(b+117,init.data+144,11);
    }
    checksum(bank,sizeof(bank));
    assert(tt_dx7_import(bank,sizeof(bank),31,&p));assert(p.data[134]==31);
    assert(!tt_dx7_import(bank,sizeof(bank),32,&p));
    tt_synth_init();
    /* INIT should stay audible while held, with headroom before the mixer. */
    tt_synth_stop(0);tt_synth_start(0,&init,60,100);
    for(int i=0;i<48000;++i)tt_synth_sample(0,0);
    double init_peak=0,init_energy=0;
    for(int i=0;i<48000;++i){double s=tt_synth_sample(0,0);init_peak=fmax(init_peak,fabs(s));init_energy+=s*s;}
    assert(init_peak>.49 && init_peak<.51);
    assert(sqrt(init_energy/48000)>.34 && sqrt(init_energy/48000)<.36);
    for(int algo=0;algo<32;++algo) {
        p=init;p.data[134]=algo;
        tt_synth_stop(0);tt_synth_start(0,&p,60,100);
        double energy=0;
        for(int i=0;i<4800;++i){float s=tt_synth_sample(0,0);assert(isfinite(s));energy+=s*s;}
        assert(energy>.001);
        tt_synth_release(0);
        for(int i=0;i<480000 && !tt_synth_finished(0);++i)tt_synth_sample(0,0);
        assert(tt_synth_finished(0));
    }
    /* A4 frequency and transpose are applied before FM, not by resampling PCM. */
    p=init;p.data[144]=36;tt_synth_stop(0);tt_synth_start(0,&p,69,127);
    int crossings=0;float prev=0;
    for(int i=0;i<48000;++i){float s=tt_synth_sample(0,0);if(prev<=0 && s>0)++crossings;prev=s;}
    assert(crossings>=878 && crossings<=882);
    /* A no-op update must retain phase, envelope progress and LFO delay,
       even when the current 64-sample block is only partially consumed. */
    TtDx7Patch live=init;live.data[139]=60;live.data[143]=4;live.data[138]=30;
    for(int c=0;c<2;++c){tt_synth_stop(c);tt_synth_start(c,&live,69,100);}
    for(int i=0;i<5013;++i)assert(tt_synth_sample(0,0)==tt_synth_sample(1,0));
    assert(tt_synth_update(0,&live));
    for(int i=0;i<5000;++i)assert(tt_synth_sample(0,0)==tt_synth_sample(1,0));
    TtDx7Patch invalid=live;invalid.data[134]=32;
    assert(!tt_synth_update(0,&invalid));assert(!tt_synth_update(-1,&live));
    for(int i=0;i<256;++i)assert(tt_synth_sample(0,0)==tt_synth_sample(1,0));
    /* Editing attack parameters during release must not restart the note. */
    tt_synth_release(0);tt_synth_release(1);
    for(int i=0;i<1000;++i)assert(tt_synth_sample(0,0)==tt_synth_sample(1,0));
    live.data[105]=0;assert(tt_synth_update(0,&live));
    for(int i=0;i<480000 && !tt_synth_finished(1);++i)
        assert(tt_synth_sample(0,0)==tt_synth_sample(1,0));
    assert(tt_synth_finished(0) && tt_synth_finished(1));
    /* Each of the six operator coarse ratios and output levels must alter
       a held note. Algorithm 31 lets us isolate each operator as a carrier. */
    for(int op=0;op<6;++op) {
        live=init;live.data[134]=31;
        for(int j=0;j<6;++j)live.data[j*21+16]=0;
        live.data[op*21+16]=99;
        for(int i=0;i<3;++i)live.data[op*21+4+i]=99;
        tt_synth_stop(0);tt_synth_start(0,&live,69,127);
        for(int i=0;i<4800;++i)tt_synth_sample(0,0);
        live.data[op*21+18]=2;assert(tt_synth_update(0,&live));
        crossings=0;prev=0;double loud=0,quiet=0;
        for(int i=0;i<48000;++i){float s=tt_synth_sample(0,0);if(prev<=0 && s>0)++crossings;prev=s;loud+=s*s;}
        assert(crossings>=878 && crossings<=882);
        live.data[op*21+16]=30;assert(tt_synth_update(0,&live));
        for(int i=0;i<128;++i)tt_synth_sample(0,0);
        for(int i=0;i<48000;++i){float s=tt_synth_sample(0,0);quiet+=s*s;}
        assert(quiet<loud*.01 && !tt_synth_finished(0));
    }
    static TtSong song,loaded;tt_song_init(&song);
    TtSongSample meta[2]={0},back[TT_SONG_SAMPLES];int count=0;char error[192];
    for(int i=0;i<2;++i){strcpy(meta[i].name,"test");strcpy(meta[i].source_name,"test.wav");strcpy(meta[i].path,"test.wav");tt_phrase_init(&meta[i].phrase,0);}
    meta[0].frames=1;meta[0].rate=48000;meta[1].instrument_type=TT_INSTRUMENT_DX7;meta[1].dx7=p;
    char path[128];snprintf(path,sizeof(path),"/tmp/tt-synth-%ld.song",(long)getpid());
    assert(tt_song_save(path,&song,meta,2,error,sizeof(error)));
    assert(tt_song_load(path,&loaded,back,&count,error,sizeof(error)));
    assert(count==2 && back[0].instrument_type==TT_INSTRUMENT_SAMPLE && back[1].instrument_type==TT_INSTRUMENT_DX7);
    assert(!memcmp(&p,&back[1].dx7,sizeof(p)));unlink(path);
    puts("DX7 SysEx validation, 32 algorithms, release, live updates, tuning and song roundtrip passed");
}
