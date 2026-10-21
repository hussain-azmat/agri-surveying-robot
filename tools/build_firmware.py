#!/usr/bin/env python3
"""Compile the firmware with arduino-cli, using the OTA slot size from partitions.csv as the size limit.

arduino-cli checks the image against the BOARD's default app size (1.25 MB) and would start
reporting "sketch too big" long before the real OTA slot in Software/Rover/Src/partitions.csv
(1.5 MB) is full. This wrapper passes the real limit, so partitions.csv stays the single source of truth.

    python tools/build_firmware.py [--cli path/to/arduino-cli] [--out build/out]

Needs: the ESP32 Arduino core 3.1.x (esp32:esp32) and the ArduinoJson 7 library, installed in arduino-cli.
    arduino-cli core install esp32:esp32@3.1.3
    arduino-cli lib install "ArduinoJson@7"
"""
import argparse
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(Path(__file__).resolve().parent))
from publish_ota import ota_slot_bytes  # noqa: E402
from stage_sketch import DEFAULT_SKETCH, stage  # noqa: E402


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("sketch", nargs="?", default=DEFAULT_SKETCH,
                    help="which sketch to build: agri_rover (the full firmware, default) or a bench sketch such as mpu")
    ap.add_argument("--cli", default="arduino-cli")
    ap.add_argument("--fqbn", default="esp32:esp32:esp32")
    ap.add_argument("--out", default=str(ROOT / "build" / "out"))
    ap.add_argument("--build-path", default=str(ROOT / "build" / "fw"))
    ap.add_argument("--stage-dir", default=str(ROOT / "build" / "stage"))
    ap.add_argument("--warnings", default="all", choices=["none", "default", "more", "all"])
    args = ap.parse_args(argv)

    slot = ota_slot_bytes(ROOT)
    sketch_dir = stage(args.sketch, args.stage_dir)  # gathers Software/*/Src into one Arduino sketch folder
    cmd = [
        args.cli, "compile", "--fqbn", args.fqbn, "--warnings", args.warnings,
        "--build-property", f"upload.maximum_size={slot}",
        "--build-path", args.build_path, "--output-dir", args.out,
        str(sketch_dir),
    ]
    print("+", " ".join(cmd))
    rc = subprocess.call(cmd)
    if rc == 0:
        img = Path(args.out) / f"{args.sketch}.ino.bin"
        size = img.stat().st_size
        print(f"\nOTA image: {img}  {size} bytes = {size * 100 // slot}% of the {slot}-byte OTA slot")
    return rc


if __name__ == "__main__":
    sys.exit(main())
