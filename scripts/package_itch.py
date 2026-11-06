#!/usr/bin/env python3
"""Package the standalone turbo-tracker web app for itch.io."""

import sys
from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile


def main() -> None:
    if len(sys.argv) != 4:
        raise SystemExit("usage: package_itch.py WEB_DIR turbo-tracker OUTPUT.zip")

    web = Path(sys.argv[1])
    app = sys.argv[2]
    output = Path(sys.argv[3])
    if app != "turbo-tracker":
        raise SystemExit(f"unknown app: {app}")

    assets = [
        "curses_terminal.js",
        "vendor/xterm.css",
        "vendor/xterm.js",
        "vendor/xterm-addon-fit.js",
        f"{app}/{app}.js",
        f"{app}/{app}.wasm",
    ]
    missing = [name for name in assets if not (web / name).is_file()]
    if missing:
        raise SystemExit("Missing built assets: " + ", ".join(missing))

    output.parent.mkdir(parents=True, exist_ok=True)
    with ZipFile(output, "w", compression=ZIP_DEFLATED, compresslevel=9) as archive:
        archive.write(web / "app.html", "index.html")
        for name in assets:
            archive.write(web / name, name)
    print(output)


if __name__ == "__main__":
    main()