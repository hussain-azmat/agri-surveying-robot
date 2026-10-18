#!/usr/bin/env python3
"""Fail if GitHub's live certificate chain no longer validates against the roots embedded in
firmware/agri_rover/certs.h. This mirrors what the ESP32 does (validate the server chain against
a fixed set of roots), so it detects a CA change BEFORE the rover's OTA/log uploads break.

Run weekly by .github/workflows/tls-roots.yml. Needs the `openssl` command line tool.

    python tools/check_tls_roots.py [host ...]      (default: api.github.com)
"""
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CERTS_H = ROOT / "firmware" / "agri_rover" / "certs.h"


def embedded_roots_pem():
    text = CERTS_H.read_text(encoding="utf-8")
    parts = re.findall(r'^\s*"(.*)\\n"\s*;?\s*$', text, flags=re.M)
    return "\n".join(parts) + "\n"


def fetch_chain(host):
    p = subprocess.run(
        ["openssl", "s_client", "-connect", f"{host}:443", "-servername", host, "-showcerts"],
        input=b"", capture_output=True, timeout=30,
    )
    out = p.stdout.decode("utf-8", "replace")
    pems = re.findall(r"-----BEGIN CERTIFICATE-----.*?-----END CERTIFICATE-----", out, flags=re.S)
    if not pems:
        raise RuntimeError(f"could not read a certificate chain from {host}: {p.stderr.decode('utf-8','replace')[:200]}")
    return pems


def check(host):
    pems = fetch_chain(host)
    with tempfile.TemporaryDirectory() as d:
        d = Path(d)
        (d / "roots.pem").write_text(embedded_roots_pem())
        (d / "leaf.pem").write_text(pems[0] + "\n")
        (d / "inter.pem").write_text("\n".join(pems[1:]) + "\n" if len(pems) > 1 else "")
        cmd = ["openssl", "verify", "-no-CApath", "-no-CAstore", "-CAfile", str(d / "roots.pem")]
        if len(pems) > 1:
            cmd += ["-untrusted", str(d / "inter.pem")]
        cmd.append(str(d / "leaf.pem"))
        p = subprocess.run(cmd, capture_output=True, timeout=30)
        ok = p.returncode == 0
        detail = (p.stdout + p.stderr).decode("utf-8", "replace").strip().splitlines()
        return ok, detail[-1] if detail else ""


def main():
    hosts = sys.argv[1:] or ["api.github.com"]
    bad = 0
    for h in hosts:
        try:
            ok, detail = check(h)
        except Exception as e:  # network problems should not look like a CA change
            print(f"[skip] {h}: {e}")
            continue
        print(f"[{'ok' if ok else 'FAIL'}] {h}: {detail}")
        bad += 0 if ok else 1
    if bad:
        print("\nGitHub's certificate chain no longer validates against the embedded roots.\n"
              "Update with tools/gen_certs.py and ship a firmware release BEFORE the old chain expires.")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
