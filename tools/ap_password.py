#!/usr/bin/env python3
"""Print a rover's setup-network name and its access code, from its MAC address.

The rover derives these itself (firmware/agri_rover/accesscode.cpp); this tool computes the same
values so you can print a sticker for each unit before it is ever powered on.

    python tools/ap_password.py A4:CF:12:34:56:78
    esptool.py read_mac   (prints the MAC)

Note: if the owner chose their own access code on the setup page, that replaces the derived one.
"""
import re
import sys
from pathlib import Path

ALPHABET = "abcdefghjkmnpqrstuvwxyz23456789"


def fnv1a(text, seed):
    h = seed
    for byte in text.encode("utf-8"):
        h ^= byte
        h = (h * 16777619) & 0xFFFFFFFF
    return h


def derive(salt, mac_hex12):
    seed = f"{salt}:{mac_hex12}"
    a = fnv1a(seed, 2166136261)
    b = fnv1a(seed, 0x9E3779B1)
    out = []
    for i in range(10):
        v = a if i < 5 else b
        out.append(ALPHABET[(v >> ((i % 5) * 6)) % 31])
    return "".join(out)


def read_salt():
    cfg = Path(__file__).resolve().parent.parent / "firmware" / "agri_rover" / "config.h"
    m = re.search(r'#define\s+AP_PASSWORD_SALT\s+"([^"]*)"', cfg.read_text(encoding="utf-8"))
    if not m:
        raise SystemExit("AP_PASSWORD_SALT not found in config.h")
    return m.group(1)


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    mac = re.sub(r"[^0-9A-Fa-f]", "", sys.argv[1]).upper()
    if len(mac) != 12:
        print("MAC must have 12 hex digits, e.g. A4:CF:12:34:56:78")
        return 2
    print(f"Rover id          : rover-{mac[6:]}")
    print(f"Setup network     : RoverSetup-{mac[8:]}")
    print(f"Access code       : {derive(read_salt(), mac)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
