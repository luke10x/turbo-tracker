# Glide (GLD)

This short song compares an immediate pitch change with a glide using the same
plain sine sample, tuned to C-4. The three patterns play in this order:

- **Direct pitch:** Row `00` starts sample `01` at `C-4`; row `02` starts it
  again at `E-4` without `GLD`.
- **Glide:** Row `00` starts at `C-4`; row `02` changes the target to `E-4`
  with `GLD04`, gliding over several ticks.
- **Faster glide:** The same phrase uses `GLD08`, twice the rate of `GLD04`.
- Song settings: 180 BPM, four rows per beat, twelve ticks per row. Each
  pattern has 32 rows.

Listen for the direct pitch jump, then compare the glide and the faster glide.
`GLD` sets the glide rate in sixty-fourths of a semitone per tick; `04` is one
sixteenth and `08` is one eighth of a semitone per tick.
