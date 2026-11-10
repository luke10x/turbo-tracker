# Effect registers (0RG–FRG)

This example uses one sine sample and keeps the volume effect reference fixed while changing the referenced register.

- Row `00`: `0RG30` writes `30` to register `0`; `MVL$0` reads it, so the note starts quietly.
- Row `04`: `0RG80` changes register `0` to `80`; the same `MVL$0` reference sets a medium level.
- Row `08`: `0RGFF` changes register `0` to `FF`; the unchanged `MVL$0` reference sets full level.

The `$0` value reference resolves when playback reaches that effect. `0RG` through `FRG` write registers `0` through `F`. Registers are shared by pattern and table playback and start at `00` when playback starts. This makes one effect slot reusable while its value changes elsewhere in the sequence.