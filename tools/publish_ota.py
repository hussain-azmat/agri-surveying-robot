#!/usr/bin/env python3
"""Publish ota/version.txt + ota/firmware.bin from a compiled firmware image.

Run by CI after compiling (see .github/workflows/build-firmware.yml). It publishes ONLY when
FW_VERSION in config.h is NEWER than the version already published, because the rovers update only
to a newer version. It refuses to publish an image that cannot fit an OTA slot.

    python tools/publish_ota.py --bin build/out/agri_rover.ino.bin [--dry-run]

Exit code 0: published, or deliberately skipped (a message says which). 1: something is wrong.
When running under GitHub Actions it also writes `published=true|false` to $GITHUB_OUTPUT.
"""
import argparse
import hashlib
import os
import re
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SEMVER = re.compile(r"^v?(\d{1,5})\.(\d{1,5})\.(\d{1,5})$")


def parse_semver(text):
    m = SEMVER.match(text.strip())
    if not m:
        raise ValueError(f"not a MAJOR.MINOR.PATCH version: {text!r}")
    return tuple(int(x) for x in m.groups())


def read_fw_version(root):
    text = (root / "Software" / "Common" / "Src" / "config.h").read_text(encoding="utf-8")
    m = re.search(r'#define\s+FW_VERSION\s+"([^"]+)"', text)
    if not m:
        raise ValueError("FW_VERSION not found in config.h")
    parse_semver(m.group(1))
    return m.group(1)


def read_manifest(path):
    if not path.exists():
        return None
    data = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if line and not line.startswith("#") and "=" in line:
            k, v = line.split("=", 1)
            data[k.strip()] = v.strip()
    return data


def ota_slot_bytes(root):
    """Size of the app0 partition in Software/Rover/Src/partitions.csv."""
    for line in (root / "Software" / "Rover" / "Src" / "partitions.csv").read_text().splitlines():
        parts = [p.strip() for p in line.split(",")]
        if len(parts) >= 5 and parts[0] == "app0":
            return int(parts[4], 0)
    raise ValueError("app0 not found in partitions.csv")


def set_output(published):
    out = os.environ.get("GITHUB_OUTPUT")
    if out:
        with open(out, "a", encoding="utf-8") as f:
            f.write(f"published={'true' if published else 'false'}\n")


def run(root, bin_path, dry_run=False):
    """Returns (exit_code, published)."""
    version = read_fw_version(root)
    if not bin_path.exists():
        print(f"ERROR: image not found: {bin_path}")
        return 1, False
    data = bin_path.read_bytes()
    slot = ota_slot_bytes(root)
    if len(data) < 64 * 1024:
        print(f"ERROR: image is only {len(data)} bytes; that is not a firmware image")
        return 1, False
    if data[0] != 0xE9:
        print("ERROR: not an ESP32 application image (magic byte is not 0xE9). Use the app .bin, not the merged one.")
        return 1, False
    if len(data) > slot:
        print(f"ERROR: image is {len(data)} bytes but an OTA slot holds {slot}. Shrink the firmware or enlarge the slots.")
        return 1, False

    ota = root / "ota"
    old = read_manifest(ota / "version.txt")
    md5 = hashlib.md5(data).hexdigest()

    if old and "version" in old:
        cmp_old = parse_semver(old["version"])
        cmp_new = parse_semver(version)
        if cmp_new < cmp_old:
            print(f"ERROR: FW_VERSION {version} is OLDER than the published {old['version']}. Rovers would ignore it.")
            return 1, False
        if cmp_new == cmp_old:
            if old.get("md5") != md5:
                print(f"NOTICE: the firmware changed but FW_VERSION is still {version}, so it was NOT published. "
                      f"Bump FW_VERSION in config.h (tools/bump_version.py) to release it.")
            else:
                print(f"Nothing to do: {version} is already published and identical.")
            return 0, False

    print(f"Publishing v{version}: {len(data)} bytes, md5 {md5} ({len(data) * 100 // slot}% of an OTA slot)")
    if dry_run:
        print("(dry run: nothing written)")
        return 0, False
    ota.mkdir(exist_ok=True)
    shutil.copyfile(bin_path, ota / "firmware.bin")
    (ota / "version.txt").write_text(f"version={version}\nmd5={md5}\nsize={len(data)}\n", encoding="utf-8", newline="\n")
    return 0, True


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--bin", required=True, help="compiled application image (agri_rover.ino.bin)")
    ap.add_argument("--root", default=str(ROOT), help="repo root (for tests)")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args(argv)
    try:
        code, published = run(Path(args.root), Path(args.bin), args.dry_run)
    except ValueError as e:
        print(f"ERROR: {e}")
        code, published = 1, False
    set_output(published)
    return code


if __name__ == "__main__":
    sys.exit(main())
