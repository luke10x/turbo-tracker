#include "tt_synth.h"
#include "tt_sample.h"
#include "synth.h"
#include "controllers.h"
#include "dx7note.h"
#include "freqlut.h"
#include "exp2.h"
#include "sin.h"
#include "lfo.h"
#include <cstring>
struct Voice { Dx7Note note; Lfo lfo; Controllers controls; int32_t buffer[N]; int at, midi_note, velocity; bool active; };
static Voice voices[TT_CHANNELS];
int tt_dx7_carrier(int algorithm,int op) {
    return algorithm>=0 && algorithm<32 && op>=0 && op<6 && FmCore::carrier(algorithm,5-op);
}
void tt_synth_init(void) {
    static bool initialized=false;
    if(initialized)return;
    Freqlut::init(48000);Exp2::init();Sin::init();Lfo::init(48000);PitchEnv::init(48000);
    initialized=true;
}
void tt_synth_start(int c,const TtDx7Patch *p,int note,int velocity) {
    if(c<0 || c>=TT_CHANNELS || !tt_dx7_valid(p))return;
    Voice &v=voices[c];v.midi_note=note;v.velocity=velocity;v.active=true;
    v.note.init((const char *)p->data,note+p->data[144]-24,velocity);
    v.lfo.reset((const char *)p->data+137);v.lfo.keydown();
    v.controls.values_[kControllerPitch]=8192;v.at=N;
}
int tt_synth_update(int c,const TtDx7Patch *p) {
    if(c<0 || c>=TT_CHANNELS || !voices[c].active || !tt_dx7_valid(p))return 0;
    Voice &v=voices[c];
    v.note.update((const char *)p->data,v.midi_note+p->data[144]-24,v.velocity);
    // reset config changes neither LFO phase nor its running delay ramp.
    v.lfo.reset((const char *)p->data+137);
    return 1;
}
int tt_synth_finished(int c) { return voices[c].note.finished(); }
void tt_synth_release(int c) { voices[c].note.keyup(); }
void tt_synth_stop(int c) { voices[c]=Voice{}; }
float tt_synth_sample(int c,double semitones) {
    Voice &v=voices[c];
    if(v.at>=N) {
        memset(v.buffer,0,sizeof(v.buffer));
        v.controls.values_[kControllerPitch]=8192+(int)(semitones*8192.0/3.0);
        v.note.compute(v.buffer,v.lfo.getsample(),v.lfo.getdelay(),&v.controls);v.at=0;
    }
    // A full-level single carrier peaks at 0.5 before tracker pan/mix gain.
    // The Android app's /2^28 scaling reserved additional global polyphony
    // headroom; our six-channel mixer already applies its own attenuation.
    return v.buffer[v.at++]/67108864.0f;
}
