# Table note-off (TBL)

This short song starts table `00` from row `00`. Its ticks play C-4, G-4 and
E-4 from the sine sample. `OFF` table notes at ticks `03`, `09` and `0E` stop
the channel's sample voice; blank ticks leave the current voice alone.

The song uses one table tick per tracker row so the note and OFF events are
easy to hear. Sample playback currently stops immediately at OFF. When release
envelopes are implemented, OFF will trigger the envelope release instead.
