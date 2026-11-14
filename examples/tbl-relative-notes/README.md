# Relative table notes (TBL)

Table `00` uses `+=0`, `+=4`, `+=7`, and `+=C` at ticks `00`, `04`, `08`, and `0C`. Each offset is a hexadecimal semitone count from the note that calls the table. The same table is called first from C-4 and then D-4, so the same arpeggio transposes with its root.

Enter relative notes in a table as `+=X` or `-=X`, where `X` is one hexadecimal digit (`0` through `F`). The table root is the note in the pattern cell carrying `TBL`.

In pattern rows, `+01` and `-01` offset the last resolved note in the same channel. Use `>05` when the offset should use the current row's note in the channel immediately to the left. The chord voicings example demonstrates `>` offsets; Same-channel note steps demonstrates `+` and `-` offsets.
