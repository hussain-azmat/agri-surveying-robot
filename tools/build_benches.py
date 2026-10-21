#!/usr/bin/env python3
"""Compile every bench sketch (mpu, gps, motors, wifi, ota) and put each image in its function's Bin/ folder.

    python tools/build_benches.py [--cli path/to/arduino-cli] [--only mpu gps]

Software/<Function>/Bin/<name>.bin is the bench sketch's application image. A bench proves ONE function on the real
rover. To flash one (the rover must already have had the full firmware flashed once, so the partition table exists):

    python -m esptool --chip esp32 --port COM5 erase_region 0xe000 0x2000      # forget which OTA slot was active
    python -m esptool --chip esp32 --port COM5 write_flash 0x10000 Software/MPU/Bin/mpu.bin

To go back to the full firmware, flash ota/firmware.bin the same way.
"""
import argparse
import shutil
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import build_firmware  # noqa: E402
from stage_sketch import DEFAULT_SKETCH, ROOT, sketches  # noqa: E402


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--cli", default="arduino-cli")
    ap.add_argument("--only", nargs="*", help="build just these benches")
    args = ap.parse_args(argv)

    benches = {n: p for n, p in sketches().items() if n != DEFAULT_SKETCH}
    if args.only:
        unknown = [n for n in args.only if n not in benches]
        if unknown:
            print(f"unknown bench: {', '.join(unknown)}. Available: {', '.join(benches)}")
            return 2
        benches = {n: benches[n] for n in args.only}

    failed = []
    for name, ino in benches.items():
        out = ROOT / "build" / "bench" / name
        print(f"\n=== bench '{name}' ===")
        rc = build_firmware.main(["--cli", args.cli, "--out", str(out), "--build-path", str(ROOT / "build" / "fw-bench"), name])
        if rc != 0:
            failed.append(name)
            continue
        dest_dir = ino.parent.parent / "Bin"
        dest_dir.mkdir(exist_ok=True)
        shutil.copy2(out / f"{name}.ino.bin", dest_dir / f"{name}.bin")
        print(f"-> {(dest_dir / f'{name}.bin').relative_to(ROOT)}")
    if failed:
        print(f"\nFAILED: {', '.join(failed)}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
