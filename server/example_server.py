"""Minimal example server for the SmallTV Pro ESPHome firmware (Python standard library only).

Serves a clock page as a draw list and counts button taps:
  GET  /api/devices/<key>/drawlist   -> {"brightness": .., "ops": [...]}
  POST /api/devices/<key>/button     <- {"event": "single" | "double"}
  GET  /                             -> short help text

Run:  python3 example_server.py [--port 8790] [--brightness 80]
Then set "server" in smalltv-pro.yaml to http://<this-machine>:8790
"""

import argparse
import json
import threading
from datetime import datetime
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

BG = [16, 16, 20]
WHITE = [235, 235, 235]
GREY = [140, 140, 150]
ACCENT = [70, 190, 210]
WEEKDAYS = ["Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"]

state_lock = threading.Lock()
taps = {}          # device key -> {"single": n, "double": n, "last": "single at 12:34:56"}
BRIGHTNESS = 80


def page(key):
    """Build the draw list for one clock. Font keys must exist in smalltv-pro.yaml."""
    now = datetime.now()
    with state_lock:
        t = dict(taps.get(key, {}))
    ops = [
        ["f", BG],                                              # always first: full background
        ["t", 120, 40, "b56", WHITE, now.strftime("%H:%M"), "c"],
        ["t", 120, 112, "b20", ACCENT, WEEKDAYS[now.weekday()], "c"],
        ["t", 120, 140, "r18", GREY, now.strftime("%d.%m.%Y"), "c"],
        ["r", 30, 176, 180, 2, [60, 60, 70]],
        ["t", 120, 190, "r14", GREY,
         f"taps {t.get('single', 0)} / double {t.get('double', 0)}", "c"],
    ]
    if t.get("last"):
        ops.append(["t", 120, 210, "r14", ACCENT, "last: " + t["last"], "c"])
    ops.append(["c", 225, 15, 5, [80, 200, 120]])               # small "alive" dot
    return {"brightness": BRIGHTNESS, "ops": ops}


class Handler(BaseHTTPRequestHandler):
    def _send(self, code, obj, ctype="application/json"):
        body = obj if isinstance(obj, bytes) else json.dumps(obj, ensure_ascii=False).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", ctype + ("; charset=utf-8" if ctype.startswith("text") or ctype.endswith("json") else ""))
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _key(self, suffix):
        parts = self.path.split("?")[0].strip("/").split("/")
        if len(parts) == 4 and parts[:2] == ["api", "devices"] and parts[3] == suffix:
            return parts[2]
        return None

    def do_GET(self):
        key = self._key("drawlist")
        if key:
            return self._send(200, page(key))
        if self.path in ("/", "/index.html"):
            return self._send(200, __doc__.encode("utf-8"), "text/plain")
        self._send(404, {"error": "not found"})

    def do_POST(self):
        key = self._key("button")
        if not key:
            return self._send(404, {"error": "not found"})
        try:
            length = int(self.headers.get("Content-Length") or 0)
            event = json.loads(self.rfile.read(length) or b"{}").get("event")
        except (ValueError, AttributeError):
            event = None
        if event not in ("single", "double"):
            return self._send(400, {"error": "event must be single or double"})
        with state_lock:
            t = taps.setdefault(key, {})
            t[event] = t.get(event, 0) + 1
            t["last"] = f"{event} at {datetime.now():%H:%M:%S}"
        print(f"{datetime.now():%H:%M:%S} {key}: {event}", flush=True)
        # This is where you would trigger your own action (log feeding, toggle a light, ...).
        self._send(200, {"ok": True, "event": event})

    def log_message(self, fmt, *args):
        pass  # keep the console quiet; the clock polls every 15 s


def main():
    global BRIGHTNESS
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--port", type=int, default=8790)
    ap.add_argument("--brightness", type=int, default=80, help="0 to 100, 0 turns the backlight off")
    args = ap.parse_args()
    BRIGHTNESS = max(0, min(100, args.brightness))
    print(f"listening on port {args.port}", flush=True)
    ThreadingHTTPServer(("", args.port), Handler).serve_forever()


if __name__ == "__main__":
    main()
