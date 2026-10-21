#!/usr/bin/env python3
"""Assemble an Arduino sketch folder from the per-function folders under Software/.

The source is organised by function (Software/MPU, Software/GPS, ...), each with its own Src/, Test/ and Bin/.
The Arduino toolchain, however, wants ONE folder that holds a sketch (<name>/<name>.ino) and every source file it uses.
This tool builds that folder: it copies the chosen .ino plus every .h/.cpp from every Software/*/Src/ (and the
partition table) into build/stage/<name>/. Nothing in it is hand-edited; it is rebuilt from scratch each time.

    python tools/stage_sketch.py                 # the full rover firmware  -> build/stage/agri_rover/
    python tools/stage_sketch.py mpu             # the MPU bench sketch     -> build/stage/mpu/
    python tools/stage_sketch.py --list          # every sketch that exists

Open build/stage/<name>/<name>.ino in the Arduino IDE to edit-compile-flash from there (edit the files in Software/,
not the staged copies: they are overwritten the next time this runs).
"""
import argparse
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SOFTWARE = ROOT / "Software"
DEFAULT_SKETCH = "agri_rover"
SOURCE_SUFFIXES = {".h", ".cpp", ".c"}
EXTRA_FILES = {"partitions.csv"}


def sketches(software=SOFTWARE):
    """{sketch name: path of its .ino}. A sketch is any Software/<Function>/Src/<name>.ino."""
    found = {}
    for ino in sorted(software.glob("*/Src/*.ino")):
        if ino.stem in found:
            raise SystemExit(f"two sketches are called {ino.stem}: {found[ino.stem]} and {ino}")
        found[ino.stem] = ino
    return found


def stage(name, out_root, software=SOFTWARE):
    """Builds out_root/<name>/ and returns its path."""
    found = sketches(software)
    if name not in found:
        raise SystemExit(f"no sketch called '{name}'. Available: {', '.join(found) or '(none)'}")
    dest = Path(out_root) / name
    if dest.exists():
        shutil.rmtree(dest)
    dest.mkdir(parents=True)
    shutil.copy2(found[name], dest / f"{name}.ino")
    seen = {}
    for src in sorted(software.glob("*/Src/*")):
        if src.suffix == ".ino" or not (src.suffix in SOURCE_SUFFIXES or src.name in EXTRA_FILES):
            continue
        if src.name in seen:
            raise SystemExit(f"{src.name} exists in two functions: {seen[src.name]} and {src}")
        seen[src.name] = src
        shutil.copy2(src, dest / src.name)
    return dest


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("sketch", nargs="?", default=DEFAULT_SKETCH)
    ap.add_argument("--out", default=str(ROOT / "build" / "stage"))
    ap.add_argument("--list", action="store_true", help="list the sketches and exit")
    args = ap.parse_args(argv)
    if args.list:
        for name, path in sketches().items():
            print(f"{name:12} {path.relative_to(ROOT)}")
        return 0
    dest = stage(args.sketch, args.out)
    print(dest)
    return 0


if __name__ == "__main__":
    sys.exit(main())
