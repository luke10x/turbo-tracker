#!/usr/bin/env python3
"""Print only the authoritative text header from a Turbo Tracker .ttt file."""
import sys


def main(path: str) -> int:
    total = 0
    with open(path, "rb") as source:
        first = source.readline(4097)
        if not first.startswith(b"F80_SAMPLE_SONG\t"):
            return 1
        line = first
        while line:
            total += len(line)
            if total > 8 * 1024 * 1024 or len(line) > 4096 or not line.endswith(b"\n"):
                return 1
            sys.stdout.buffer.write(line)
            if line.rstrip(b"\r\n") == b"END":
                return 0
            line = source.readline(4097)
    return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1]))
