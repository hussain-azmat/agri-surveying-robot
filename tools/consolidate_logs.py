#!/usr/bin/env python3
"""Merge the rovers' uploaded log chunks into ONE file per rover per day.

Rovers upload small chunks (logs/<rover>/<YYYY-MM-DD>/<seq>_<n>.csv) because an ESP32 cannot append to
a growing file over the GitHub API without re-sending the whole thing each time. This script (run
nightly by .github/workflows/consolidate-logs.yml) turns each finished day's chunks into
logs/<rover>/<YYYY-MM-DD>.csv and removes the chunk files.

Every chunk is a complete little file: optional "# ..." metadata lines (schema, firmware, rover, settings), then the
column header line, then rows. In the merged file the metadata lines are kept (a repeated, identical line is
dropped), the header appears once, and the rows follow in order. Tools that read CSV skip the "# " lines
(pandas.read_csv(path, comment="#")).

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


def split_file(lines):
    """(lead, header, body): the '#' lines before the header, the header line, and everything after it."""
    lead, header, body = [], None, []
    for line in lines:
        if not line.strip():
            continue
        if header is None:
            if line.startswith("#"):
                lead.append(line)
            else:
                header = line
        else:
            body.append(line)
    return lead, header, body


def merge_into(lead, body, new_lines, header):
    """Appends rows to body, dropping repeated headers and a '#' line identical to the last '#' line kept."""
    last_meta = next((l for l in reversed(body) if l.startswith("#")), lead[-1] if lead else None)
    for line in new_lines:
        if line == header:
            continue
        if line.startswith("#"):
            if line == last_meta:
                continue
            last_meta = line
        body.append(line)


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
            lead, header, body = [], None, []
            if target.exists():
                lead, header, body = split_file(target.read_text(encoding="utf-8").splitlines())
            used = []
            for c in chunks:
                c_lead, c_header, c_body = split_file(c.read_text(encoding="utf-8", errors="replace").splitlines())
                if c_header is None:  # empty, or only metadata: nothing to merge
                    used.append(c)
                    continue
                if header is None:
                    header, lead = c_header, c_lead
                    c_lead = []
                if c_header != header:
                    problems.append(f"{c}: header differs from the rest of the day; left in place")
                    continue
                merge_into(lead, body, c_lead + c_body, header)
                used.append(c)
            if header is None or not used:
                continue
            target.write_text("\n".join(lead + [header] + body) + "\n", encoding="utf-8", newline="\n")
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
