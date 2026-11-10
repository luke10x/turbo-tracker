# Beat-sliced break

This song reslices `tgndshtn-part1.wav` into 15 equal slices. The new markers
align each slice to one pattern row at 144 BPM with eight rows per beat.

- Rows `00`–`0E` play the 15 slices forward, one per row.
- Row `0F` starts the whole break forward with `SLC0F`.
- Row `1E` starts the whole break backwards with `SLCF0`.
- Rows `2D`–`3B` shuffle single- and two-slice spans in forward and reverse
  directions, mixing the break accents instead of repeating one descending run.

The full forward and reverse phrases each take 15 rows, matching the sample's
duration. Listen for the reordered, reversed accents in the final phrase.
