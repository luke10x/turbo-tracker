# Sample tracker effect status

This tracks the commands in [the effect specification](tracker-effects-and-tables.md).
“Working” means the command can be entered, saved, loaded, and run by the sample
tracker. “Partial” identifies a context or behavior still missing. The tracker
has three effect slots per pattern cell and six slots per table tick.

| Command | Pattern | Table | Status / remaining work |
| --- | --- | --- | --- |
| `G` GLD | Working | Working | Glide a playing voice to the entered note; rate `xx/64` semitones per tick. If no voice is active, start the note. |
| `H` HPC | — | — | High-pass filter not implemented. |
| `I` IIN | Working | Working | Start at zero gain and fade in by `xx/255` per tick. |
| `J` JDN | Working | Working | Pitch down by `xx/64` semitones per tick. |
| `K` KUP | Working | Working | Pitch up by `xx/64` semitones per tick. |
| `L` LPC | — | — | Low-pass filter not implemented. |
| `M` MVL | Working | Working | Set channel gain to `xx/255`. |
| `N` NPN | Working | Working | Sine auto-pan; high nibble speed, low nibble depth. |
| `O` OUT | Working | Working | Fade out by `xx/255` per tick. |
| `P` PAN | Working | Working | `00` left, `80` near center, `FF` right. |
| `Q` QRS | — | — | Filter resonance not implemented. |
| `R` RRR | Working | Working | Pitch-preserving granular time stretch; `80` normal duration, `40` about 2×, `C0` about ½×. Persists per channel until changed or playback stops. Repeat a table note to retrigger. |
| `S` SLC | Working | Working | `xy` plays from marker `x` to marker `y`. If `x > y`, playback runs backward. Equal markers are silent. |
| `T` TBL | Working | Working | Pattern invokes table; table queues its successor. |
| `U` UDL | Working | Working | Delay a note by `xx/FF` of its pattern row or table tick; `80` is about half and `55` is one third. |
| `V` VIB | Working | Working | Sine vibrato; high nibble speed, low nibble depth. |
| `W` WBL | Working | Working | Sine tremolo; high nibble speed, low nibble depth. |
| `X` XJP | Working | Working | Pattern mode jumps directly; Song mode jumps to the next active order referencing the target. Table mode jumps immediately to tick `00` of the target table. |
| `Y` YRJ | Working | Working | Absolute row/tick jump. A backward table jump loops until a pattern note or `OFF` in that channel breaks it. |
| `Z` ZTM | Working | Working | In patterns, sets runtime BPM (`xx` is hexadecimal BPM; `00` restores the song BPM). In tables, sets ticks per row (`01`–`30` hex; `00` restores the song setting). |
| `0`–`F` slots | — | — | Slot writers and `$N` readers not implemented. |

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
uses the same engine.
