#!/usr/bin/env python3
"""Package one standalone F80 browser showcase for itch.io."""

import sys
from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile


APPS = {
    "showcases/brokerage": "brokerage",
    "showcases/fm-tracker": "fm-tracker",
    "showcases/sample-tracker": "sample-tracker",
}


def main() -> None:
    if len(sys.argv) != 4 or sys.argv[2] not in APPS:
        raise SystemExit(
            "usage: package_itch.py WEB_DIR showcases/{brokerage,fm-tracker,sample-tracker} OUTPUT.zip"
        )

    web = Path(sys.argv[1])
    app = sys.argv[2]
    output = Path(sys.argv[3])
    page = (web / "app.html").read_text(encoding="utf-8")
    selector = "const applicationId = new URLSearchParams(location.search).get('app');"
    if page.count(selector) != 1:
        raise SystemExit("Cannot locate the app selector in app.html")
    page = page.replace(selector, f"const applicationId = '{app}';")

    stem = APPS[app]
    assets = [
        "curses_terminal.js",
        "vendor/xterm.css",
        "vendor/xterm.js",
        "vendor/xterm-addon-fit.js",
        f"{app}/{stem}.js",
        f"{app}/{stem}.wasm",
    ]
    missing = [name for name in assets if not (web / name).is_file()]
    if missing:
        raise SystemExit("Missing built assets: " + ", ".join(missing))

    output.parent.mkdir(parents=True, exist_ok=True)
    with ZipFile(output, "w", compression=ZIP_DEFLATED, compresslevel=9) as archive:
        archive.writestr("index.html", page)
        for name in assets:
            archive.write(web / name, name)
    print(output)


if __name__ == "__main__":
    main()
