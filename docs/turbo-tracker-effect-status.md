# Tracker effect status

This tracks the commands in [the effect specification](tracker-effects-and-tables.md).
“Working” means the command can be entered, saved, loaded, and run by the
tracker. “Partial” identifies a context or behavior still missing. The tracker
has three effect slots per pattern cell and six slots per table tick.

The **Pattern** and **Table** columns apply to sample instruments and are the
same for MSFA/DX7 synth voices. The **Synth** column states whether the
command actually does something on a DX7 instrument:

- **Yes** — works on DX7 voices through the shared channel mixer (gain, pan,
  pitch modulation, glide, note delay) or at the playback layer (flow, tempo,
  registers).
- **Sample only** — the command is entered, saved, and loaded, but only affects
  sample playback; DX7 voices ignore it.
- **—** — not implemented in the engine at all.

| Command | Pattern | Table | Synth | Status / remaining work |
| --- | --- | --- | --- | --- |
| `G` GLD | Working | Working | Yes | Glide a playing voice to the entered note; rate `xx/64` semitones per tick. If no voice is active, start the note. On a DX7 voice the glide is relative to the note the synth started on. |
| `H` HPC | — | — | — | High-pass filter not implemented. |
| `I` IIN | Working | Working | Yes | Start at zero gain and fade in by `xx/255` per tick. |
| `J` JDN | Working | Working | Yes | Pitch down by `xx/64` semitones per tick. |
| `K` KUP | Working | Working | Yes | Pitch up by `xx/64` semitones per tick. |
| `L` LPC | — | — | — | Low-pass filter not implemented. |
| `M` MVL | Working | Working | Yes | Set channel gain to `xx/255`. |
| `N` NPN | Working | Working | Yes | Sine auto-pan; high nibble speed, low nibble depth. |
| `O` OUT | Working | Working | Yes | Fade out by `xx/255` per tick. |
| `P` PAN | Working | Working | Yes | `00` left, `80` near center, `FF` right. |
| `Q` QRS | — | — | — | Filter resonance not implemented. |
| `R` RRR | Working | Working | Sample only | Pitch-preserving granular time stretch; `80` normal duration, `40` about 2×, `C0` about ½×. Persists per channel until changed or playback stops. Repeat a table note to retrigger. DX7 voices ignore the setting. |
| `S` SLC | Working | Working | Sample only | `xy` plays from marker `x` to marker `y`. If `x > y`, playback runs backward. Equal markers are silent. DX7 voices start from the note only and ignore the marker range. |
| `T` TBL | Working | Working | Yes | Pattern invokes table; table queues its successor. |
| `U` UDL | Working | Working | Yes | Delay a note by `xx/FF` of its pattern row or table tick; `80` is about half and `55` is one third. Delayed DX7 note-ons are supported. |
| `V` VIB | Working | Working | Yes | Sine vibrato; high nibble speed, low nibble depth. |
| `W` WBL | Working | Working | Yes | Sine tremolo; high nibble speed, low nibble depth. |
| `X` XJP | Working | Working | Yes | Pattern mode jumps directly; Song mode jumps to the next active order referencing the target. Table mode jumps immediately to tick `00` of the target table. |
| `Y` YRJ | Working | Working | Yes | Absolute row/tick jump. A backward table jump loops until a pattern note or `OFF` in that channel breaks it. |
| `Z` ZTM | Working | Working | Yes | In patterns, sets runtime BPM (`xx` is hexadecimal BPM; `00` restores the song BPM). In tables, sets ticks per row (`01`–`30` hex; `00` restores the song setting). |
| `0`–`F` registers | Working | Working | Yes | Pattern writers are `0RGxx`–`FRGxx`; table slots use register digit `0`–`F` with value `xx`. Any effect may read a register with `$x`. Values resolve during playback; the register bank is shared across channels and contexts and resets to `00` on playback start. |

For `N`, `V`, and `W`, the high nibble advances a sine phase by that many
sixty-fourths of a cycle per tick. `V` depth `F` is approximately ±1.25
semitones, based on the period modulation range in the local `pt2-clone`
replayer (`../../pt2-clone/src/pt2_replayer.c`, `vibrato2` and `tremolo`).
`W` depth `F` reaches a full-scale gain swing before clamping to
the output range. `N` depth `F` moves up to half the stereo width from the
fixed pan position. These commands keep their state until changed or
playback stops. Their phase timing follows the PT2 nibble scale; this is a
sample engine, so its pitch and gain calculations are not bit-exact Paula
emulation. `J` and `K` pitch offset is limited to eight octaves. Tables have
256 ticks (`00`–`FF`); backward `Y` jumps repeat until a pattern note or `OFF`
breaks the loop.

The native test suite covers command text round trips, tick modulation,
glide, panning, retrigger timing, and reverse slice bounds. The WASM build
uses the same engine. Synth coverage follows the common mixer: gain, pan,
pitch, glide, and delay effects feed the DX7 voices, while slice selection
and time stretch remain sample-only (see the Synth column).
