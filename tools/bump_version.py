#!/usr/bin/env python3
"""Bump FW_VERSION in firmware/agri_rover/config.h.

    python tools/bump_version.py patch     0.1.0 -> 0.1.1   (bug fix)
    python tools/bump_version.py minor     0.1.3 -> 0.2.0   (new feature)
    python tools/bump_version.py major     0.4.2 -> 1.0.0
    python tools/bump_version.py 0.3.0                      (set explicitly)

Then commit and push: CI compiles the firmware and publishes ota/version.txt + ota/firmware.bin,
and every rover updates the next time it is stopped and online.
"""
import re
import sys
from pathlib import Path

CONFIG = Path(__file__).resolve().parent.parent / "firmware" / "agri_rover" / "config.h"
SEMVER = re.compile(r"^(\d{1,5})\.(\d{1,5})\.(\d{1,5})$")


def bump(current, how):
    major, minor, patch = (int(x) for x in current.split("."))
    if how == "patch":
        return f"{major}.{minor}.{patch + 1}"
    if how == "minor":
        return f"{major}.{minor + 1}.0"
    if how == "major":
        return f"{major + 1}.0.0"
    if SEMVER.match(how):
        return how
    raise ValueError(f"expected patch, minor, major or x.y.z, got {how!r}")


def main(argv=None, config=CONFIG):
    argv = sys.argv[1:] if argv is None else argv
    if len(argv) != 1:
        print(__doc__)
        return 2
    text = Path(config).read_text(encoding="utf-8")
    m = re.search(r'(#define\s+FW_VERSION\s+")([^"]+)(")', text)
    if not m or not SEMVER.match(m.group(2)):
        print("ERROR: FW_VERSION not found or not x.y.z in config.h")
        return 1
    try:
        new = bump(m.group(2), argv[0])
    except ValueError as e:
        print(f"ERROR: {e}")
        return 1
    Path(config).write_text(text[: m.start(2)] + new + text[m.end(2):], encoding="utf-8", newline="\n")
    print(f"FW_VERSION {m.group(2)} -> {new}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
