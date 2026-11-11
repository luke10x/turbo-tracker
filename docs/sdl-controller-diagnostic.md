# SDL controller diagnostic

Build the standalone SDL 2 and SDL 3 probes:

```sh
make controller-diagnostic
```

Run one probe at a time from the same Terminal session used for Turbo Tracker:

```sh
build/macos/bin/sdl3-controller-diagnostic
```

```sh
build/macos/bin/sdl2-controller-diagnostic
```

Each probe prints SDL's device enumeration, controller mapping status, device
IDs, and whether the Gamepad/GameController and raw Joystick APIs can open the
device. It then prints button, axis, and hat events. Press buttons and move the
sticks/triggers to see which SDL events arrive. Stop it with Ctrl+C. Use
`--scan-only` to print device enumeration and exit without waiting for input.

On macOS, compare the SDL 3 scan with SDL's `GCController` backend enabled and
disabled:

```sh
build/macos/bin/sdl3-controller-diagnostic --scan-only
SDL_JOYSTICK_MFI=0 build/macos/bin/sdl3-controller-diagnostic --scan-only
```

The second command bypasses SDL's `GCController` path and checks its HID input
path instead. Turbo Tracker does this automatically on macOS.

Running the same probe under both SDL versions helps separate SDL 3-specific
behavior from SDL's shared macOS device access. SDL 2 uses the legacy
GameController event names; SDL 3 uses its Gamepad API.
