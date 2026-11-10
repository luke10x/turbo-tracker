# Relative table notes (TBL)

Table `00` uses `+=0`, `+=4`, `+=7`, and `+=C` at ticks `00`, `04`, `08`, and `0C`. Each offset is a hexadecimal semitone count from the note that calls the table. The same table is called first from C-4 and then D-4, so the same arpeggio transposes with its root.

Enter relative notes in the note field as `+=X` or `-=X`, where `X` is one hexadecimal digit (`0` through `F`). In tables the root is the note in the pattern cell carrying `TBL`. In ordinary pattern rows, each relative note uses the sounding note in the channel immediately to its left; this also lets offsets chain across channels.
