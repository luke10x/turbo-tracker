#ifndef TT_SYNTH_H
#define TT_SYNTH_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum { TT_INSTRUMENT_SAMPLE, TT_INSTRUMENT_DX7 };
enum { TT_DX7_BYTES=156, TT_DX7_PARAMETERS=145 };
typedef struct { uint8_t data[TT_DX7_BYTES]; } TtDx7Patch;
void tt_dx7_init(TtDx7Patch *patch);
int tt_dx7_max(int parameter);
int tt_dx7_valid(const TtDx7Patch *patch);
/* Yamaha single voice (163 bytes) or 32-voice bank (4104 bytes). */
int tt_dx7_import(const uint8_t *data,size_t size,int voice,TtDx7Patch *patch);
int tt_dx7_carrier(int algorithm,int operator_index);
void tt_synth_init(void);
void tt_synth_start(int channel,const TtDx7Patch *patch,int note,int velocity);
int tt_synth_update(int channel,const TtDx7Patch *patch);
void tt_synth_release(int channel);
void tt_synth_stop(int channel);
int tt_synth_finished(int channel);
float tt_synth_sample(int channel,double semitones);
#ifdef __cplusplus
}
#endif
#endif
