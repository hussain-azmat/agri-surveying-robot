#!/usr/bin/env python3
"""Merge the rovers' uploaded log chunks into ONE file per rover per day.

Rovers upload small chunks (logs/<rover>/<YYYY-MM-DD>/<seq>_<n>.csv) because an ESP32 cannot append to
a growing file over the GitHub API without re-sending the whole thing each time. This script (run
nightly by .github/workflows/consolidate-logs.yml) turns each finished day's chunks into
logs/<rover>/<YYYY-MM-DD>.csv and removes the chunk files.

Today's (UTC) folder is left alone because rovers may still be uploading into it. A day that gets
late chunks (a rover that was offline) is simply merged into the existing day file next run.
Chunks whose header differs from the day file are left in place and reported, never mixed in.

    python tools/consolidate_logs.py [--logs logs] [--today YYYY-MM-DD]
"""
import argparse
import datetime
import re
import sys
from pathlib import Path

DAY = re.compile(r"^\d{4}-\d{2}-\d{2}$")


def consolidate(logs_root, today):
    """Returns (merged_days, problems)."""
    merged, problems = [], []
    if not logs_root.is_dir():
        return merged, problems
    for rover in sorted(p for p in logs_root.iterdir() if p.is_dir()):
        for day_dir in sorted(p for p in rover.iterdir() if p.is_dir()):
            name = day_dir.name
            if name != "unsynced" and (not DAY.match(name) or name >= today):
                continue
            chunks = sorted(day_dir.glob("*.csv"))
            if not chunks:
                try:
                    day_dir.rmdir()
                except OSError:
                    pass
                continue
            target = rover / f"{name}.csv"
            header, rows = None, []
            if target.exists():
                existing = target.read_text(encoding="utf-8").splitlines()
                if existing:
                    header, rows = existing[0], [l for l in existing[1:] if l.strip()]
            used = []
            for c in chunks:
                lines = c.read_text(encoding="utf-8", errors="replace").splitlines()
                if not lines:
                    used.append(c)
                    continue
                if header is None:
                    header = lines[0]
                if lines[0] != header:
                    problems.append(f"{c}: header differs from the rest of the day; left in place")
                    continue
                rows.extend(l for l in lines[1:] if l.strip())
                used.append(c)
            if header is None or not used:
                continue
            target.write_text(header + "\n" + "\n".join(rows) + "\n", encoding="utf-8", newline="\n")
            for c in used:
                c.unlink()
            if not any(day_dir.iterdir()):
                day_dir.rmdir()
            merged.append(str(target))
    return merged, problems


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--logs", default=str(Path(__file__).resolve().parent.parent / "logs"))
    ap.add_argument("--today", default=datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%d"))
    args = ap.parse_args(argv)
    merged, problems = consolidate(Path(args.logs), args.today)
    for m in merged:
        print("merged", m)
    for p in problems:
        print("WARNING:", p)
    print(f"{len(merged)} day file(s) written")
    return 0


if __name__ == "__main__":
    sys.exit(main())
