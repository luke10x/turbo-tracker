# ZTM — pattern BPM and table tick rate

This short example shows that `ZTM` depends on where it appears. The song starts at 80 BPM with 12 ticks per row.

- Pattern row `00`, `ZTM78` sets the BPM to hexadecimal `78` (120).
- Pattern row `04`, `ZTM00` restores the song BPM (80).
- Pattern row `08` starts table `00`. Its `Z03` at tick `00` changes the table tick rate to 3 ticks per row, spreading the arpeggio notes farther apart. `Z0C` at tick `08` restores 12 ticks per row, so the notes move faster again.

Pattern ZTM values are BPM in hexadecimal (`1E`–`FF`); `00` restores the song setting. Table ZTM values are ticks per row (`01`–`30` hex); `00` restores the song setting. The song timing settings themselves are unchanged.
