# MSFA core

Source: https://github.com/google/music-synthesizer-for-android
Revision: f67d41d313b7dc85f6fb99e79e515cc9d208cfff
License: Apache 2.0 (see LICENSE and source headers).

Only the synthesis core is vendored. No Android app, effects/filter, presets,
SDK, or platform MIDI code is included. Turbo Tracker's C ABI adapter lives in
src/tt_synth.cpp; patch validation and SysEx decoding live in src/tt_dx7.c.

Local changes:
- aligned_buf.h includes stddef.h for size_t under Emscripten.
- Envelope rate increments are scaled from 44.1 kHz to the tracker's 48 kHz.
- Oscillator key sync is honored; feedback buffers are initialized per note.
- LFO amplitude depth and operator amplitude sensitivity attenuate operator
  envelopes (approximate DX7 AMS depths; not claimed bit-exact hardware emulation).
- Envelope/carrier completion queries reclaim released voices. Nonzero L4
  continues sounding, as specified by the patch, until replaced or stopped.
- Frequency lookup guards extreme pitch shifts against undefined shifts.

Build with wrapping signed arithmetic, matching the upstream fixed-point DSP.
The FM render block is 64 samples. Live patch updates retain oscillator phases,
envelope stages/key state, and LFO phase/delay progress. Output/scaling changes
shift the current envelope amplitude and retarget the current stage; the FM core
ramps the resulting gain across a block. Coarse/fine ratios retune the oscillator
without phase reset. Sync switches only affect subsequent note-on events.

The adapter converts core output using /2^26 (a full-level carrier peaks at
0.5), rather than carrying over the Android application's /2^28 global
polyphony headroom on top of the tracker's channel mixer attenuation.
