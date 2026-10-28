#!/usr/bin/env python3
"""Preview and test the rover's control page on a laptop, with no rover.

    python tools/ui_preview.py [--port 8765]        then open http://localhost:8765

It extracts the page from Software/Remote/Src/ui.h and serves it next to a SIMULATED rover that speaks the same
/api/ as the firmware (remote.cpp): arm / disarm / stop, drive (the rover moves on the map), path record / load /
auto, update check, and so on. Nothing here talks to real hardware. Edit ui.h, reload the browser, repeat.

For automated checks, /__drives returns the last drive commands the page sent (t = throttle, s = turn).
"""
import argparse
import json
import math
import re
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

ROOT = Path(__file__).resolve().parent.parent
UI_H = ROOT / "Software" / "Remote" / "Src" / "ui.h"


def extract_page(name="UI_CONTROL_HTML", ui_h=UI_H):
    """The raw HTML of one of the pages embedded in ui.h."""
    text = ui_h.read_text(encoding="utf-8")
    m = re.search(r"static const char " + name + r'\[\] PROGMEM = R"HTML\((.*?)\)HTML";', text, re.S)
    if not m:
        raise SystemExit(f"{name} not found in {ui_h}")
    return m.group(1)


class SimRover:
    """Just enough of the rover to exercise every control on the page."""

    def __init__(self):
        self.lock = threading.Lock()
        self.mode, self.reason = "DISARMED", ""
        self.lat, self.lon, self.yaw = 31.5204000, 74.3587000, 90.0
        self.t = self.s = 0.0
        self.last_drive = 0.0
        self.last_step = time.time()
        self.rec, self.rec_n = False, 0
        self.paths = ["example_square", "north_field"]
        self.loaded = ""
        self.wp = 0
        self.t0 = time.time()
        self.drives = []
        self.run_start = time.time()

    def step(self):
        now = time.time()
        dt, self.last_step = min(now - self.last_step, 0.5), now
        if self.mode == "MANUAL" and now - self.last_drive > 0.5:
            self.t = self.s = 0.0  # the real rover stops when commands go quiet
        if self.mode == "AUTO":
            self.t, self.s = 0.5, 0.15 * math.sin(now)
            self.wp = min(10, int((now - self.auto_started) / 2))
            if self.wp >= 10:
                self.mode, self.reason, self.t, self.s = "DISARMED", "PATH_DONE", 0.0, 0.0
        if self.mode == "DISARMED":
            self.t = self.s = 0.0
        self.yaw = (self.yaw + self.s * 70 * dt) % 360
        v = self.t * 1.2  # m/s
        self.lat += v * dt * math.cos(math.radians(self.yaw)) / 110540
        self.lon += v * dt * math.sin(math.radians(self.yaw)) / (111320 * math.cos(math.radians(self.lat)))
        if self.rec:
            self.rec_n += int(abs(v) * dt * 3 + (1 if v else 0) * (now % 1 < dt))
        return v

    def wheels(self):
        left, right = self.t + self.s, self.t - self.s
        m = max(abs(left), abs(right), 1.0)
        return [left / m, right / m, left / m, right / m]

    def status(self):
        with self.lock:
            v = self.step()
            now = time.time()
            return {
                "id": "rover-SIM001", "ver": "0.3.0", "net": "ONLINE", "ip": "192.168.1.50", "host": "rover-sim001.local",
                "clock": True, "mode": self.mode, "reason": self.reason, "linkMs": int((now - self.last_drive) * 1000),
                "up": int(now - self.t0), "heap": 181234, "cap": 1.0, "rssi": -58,
                "gps": {"ok": True, "fix": True, "q": 1, "sats": 9, "hdop": 1.1, "lat": round(self.lat, 7),
                        "lon": round(self.lon, 7), "spd": round(abs(v), 2), "hz": 5.0, "baud": 38400},
                "imu": {"ok": True, "cal": True, "calibrating": False, "yawOk": True, "yaw": round(self.yaw),
                        "tilt": 1, "noise": 0.04},
                "drift": {"ok": self.mode == "MANUAL" and abs(self.t) > 0.1 and abs(self.s) < 0.02 and now - self.run_start > 1.0,
                          "deg": round(0.1 * (now - self.run_start), 2), "s": round(now - self.run_start, 1)},
                "follow": {"state": "RUNNING" if self.mode == "AUTO" else "IDLE", "wp": self.wp, "n": 10,
                           "xte": 0.4, "dist": 3.2},
                "rec": self.rec, "recN": self.rec_n, "path": self.loaded,
                "log": {"bytes": 2048, "files": 0, "rows": 412, "full": False, "err": "", "tok": False, "mins": 17, "drop": 0,
                        "every": 1, "always": False},
                "ota": {"state": "idle", "msg": "up to date", "pct": 0, "remote": "0.3.0", "avail": False, "auto": True},
                "wheels": [round(w, 2) for w in self.wheels()],
            }

    def act(self, path, args):
        with self.lock:
            now = time.time()
            if path == "arm":
                if self.mode != "DISARMED":
                    return {"ok": False, "msg": "not armed: already armed"}
                self.mode, self.reason, self.last_drive = "MANUAL", "", now
                return {"ok": True, "msg": "armed"}
            if path in ("disarm", "stop"):
                self.mode, self.reason, self.t, self.s = "DISARMED", "USER_STOP" if path == "stop" else "USER_DISARM", 0, 0
                self.rec = False
                return {"ok": True, "msg": "stopped" if path == "stop" else "disarmed"}
            if path == "drive":
                if self.mode != "MANUAL":
                    return {"ok": False, "msg": "not in manual mode"}
                self.t = max(-1.0, min(1.0, float(args.get("t", 0))))
                self.s = max(-1.0, min(1.0, float(args.get("s", 0))))
                self.last_drive = now
                if abs(self.s) >= 0.02 or abs(self.t) < 0.1:
                    self.run_start = now
                self.drives.append({"t": self.t, "s": self.s, "at": round(now, 3)})
                del self.drives[:-300]
                return None
            if path == "rec/start":
                self.rec, self.rec_n = True, 0
                return {"ok": True, "msg": "recording"}
            if path == "rec/stop":
                name = args.get("name") or "path_%d" % (len(self.paths) + 1)
                self.rec = False
                self.paths.append(name)
                return {"ok": True, "msg": f"saved {name} ({self.rec_n} points)"}
            if path == "path/load":
                self.loaded = args.get("name", "")
                return {"ok": True, "msg": f"loaded {self.loaded}"}
            if path == "auto/start":
                if not self.loaded:
                    return {"ok": False, "msg": "load a path first"}
                self.mode, self.auto_started, self.wp = "AUTO", now, 0
                return {"ok": True, "msg": "auto mode started"}
            if path == "ota/check":
                return {"ok": True, "msg": "up to date (v0.3.0)"}
            if path == "ota/install":
                return {"ok": True, "msg": "already up to date"}
            if path in ("config/sync", "imu/calibrate", "bench", "hb"):
                return {"ok": True, "msg": "done (simulated)"}
        return {"ok": False, "msg": "unknown endpoint " + path}


def make_handler(rover, page):
    # `page` is a function, so every reload shows the current ui.h
    class H(BaseHTTPRequestHandler):
        def log_message(self, *a):
            pass

        def send(self, code, body, ctype="application/json"):
            data = body.encode() if isinstance(body, str) else body
            self.send_response(code)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(data)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(data)

        def route(self, method):
            u = urlparse(self.path)
            args = {k: v[0] for k, v in parse_qs(u.query).items()}
            if u.path == "/" and method == "GET":
                return self.send(200, page(), "text/html; charset=utf-8")
            if u.path == "/__drives":
                return self.send(200, json.dumps(rover.drives))
            if not u.path.startswith("/api/"):
                return self.send(404, "not found", "text/plain")
            p = u.path[5:]
            if p == "status" and method == "GET":
                return self.send(200, json.dumps(rover.status()))
            if p == "paths" and method == "GET":
                return self.send(200, json.dumps([{"name": n, "pending": False} for n in rover.paths]))
            r = rover.act(p, args)
            if r is None:
                return self.send(204, "")
            self.send(200 if r["ok"] else 409, json.dumps(r))

        def do_GET(self):
            self.route("GET")

        def do_POST(self):
            self.route("POST")

    return H


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", type=int, default=8765)
    args = ap.parse_args(argv)
    srv = ThreadingHTTPServer(("127.0.0.1", args.port), make_handler(SimRover(), extract_page))
    print(f"Simulated rover on http://localhost:{args.port}  (Ctrl+C to stop)", flush=True)
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
