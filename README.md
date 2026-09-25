# Turbo Tracker

A small Turbo Vision version of the curses tracker. It uses the local
`../tvision` checkout as a CMake subproject.

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## Run

```sh
./build/turbo-tracker
```

Keys:

- `Up`/`k`: move up
- `Down`/`j`: move down
- `Space`: toggle the selected item
- `q` or `Alt-X`: quit

## Notes

This is the same tiny checklist idea as `../curses-tracker`, but ported to
Turbo Vision widgets and event handling. For now this project is native-only:
Turbo Vision itself is a full terminal UI framework backed by ncurses on Unix,
while the web build in `../curses-tracker` works because it uses a small custom
curses shim that writes directly to xterm.js.
