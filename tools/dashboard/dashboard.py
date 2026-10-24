#!/usr/bin/env python3
"""Workshop live dashboard.

Listens for UDP broadcast packets from the person-detector boards and serves a
projector-friendly web page that updates live over Server-Sent Events.

    python3 dashboard.py            # real boards
    python3 dashboard.py --demo     # fake teams, for rehearsal without hardware
    python3 dashboard.py --no-video # status only, no live camera view

Live video: the dashboard sends "watch" to each board's UDP port 5006 once a second;
the board answers by pushing every new 96x96 JPEG frame (one UDP packet each) back here.

Standard library only - no pip install needed.
"""

import argparse
import base64
import json
import os
import random
import re
import socket
import string
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

OFFLINE_AFTER_S = 5.0
MAX_EVENTS = 40
MAX_TEAM_LEN = 24
VIDEO_PORT = 5006
VIDEO_STALE_S = 3.0

lock = threading.Lock()
teams = {}          # mac -> dict
first_online = []   # macs, in the order they first appeared
events = []         # newest last: {"t": epoch, "kind": str, "text": str}
frames = {}         # mac -> (seq, jpeg bytes, time received)
# Team names set on the dashboard (✎ on a tile). They win over the name the board sends,
# so boards without a working console can still be named. Kept across dashboard restarts.
NAMES_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "team_names.json")
TEAM_NAME_RE = re.compile(r"^[A-Za-z0-9 _.-]{1,%d}$" % MAX_TEAM_LEN)
names = {}          # mac -> name


def load_names():
    try:
        with open(NAMES_FILE) as f:
            data = json.load(f)
        names.update({m: n for m, n in data.items() if isinstance(n, str) and TEAM_NAME_RE.match(n)})
    except (OSError, ValueError):
        pass


def save_names():
    tmp = NAMES_FILE + ".tmp"
    with open(tmp, "w") as f:
        json.dump(names, f, indent=1)
    os.replace(tmp, NAMES_FILE)


def rename(mac, name):
    """Set (or with "" clear) a dashboard team name. Returns False for an unknown board."""
    with lock:
        t = teams.get(mac)
        if t is None:
            return False
        old = t["label"]
        if name:
            names[mac] = name
        else:
            names.pop(mac, None)
        save_names()
        t["name"] = names.get(mac) or t.get("board_name", "")
        t["label"] = display_name(mac, t["name"])
        if t["label"] != old:
            add_event("online", f"{old} is now team {t['label']}")
            log(f"renamed {mac}: {old!r} -> {t['label']!r}")
    return True


def log(msg):
    print(time.strftime("%H:%M:%S"), msg, flush=True)


def add_event(kind, text):
    events.append({"t": time.time(), "kind": kind, "text": text})
    del events[:-MAX_EVENTS]


def clean_str(value, limit):
    s = "".join(ch for ch in str(value) if ch.isprintable())
    return s.strip()[:limit]


def to_int(value, default=0, lo=None, hi=None):
    try:
        v = int(value)
    except (TypeError, ValueError):
        return default
    if lo is not None:
        v = max(lo, v)
    if hi is not None:
        v = min(hi, v)
    return v


def display_name(mac, name):
    """Team names may collide; disambiguate with the last 4 hex digits of the MAC."""
    if not name:
        return f"no team name ·{mac.replace(':', '')[-4:]}"
    clash = any(t["name"] == name for m, t in teams.items() if m != mac)
    return f"{name} ·{mac.replace(':', '')[-4:]}" if clash else name


def handle_packet(data, addr):
    try:
        msg = json.loads(data.decode("utf-8"))
    except (UnicodeDecodeError, ValueError):
        return
    if not isinstance(msg, dict):
        return
    mac = clean_str(msg.get("mac", ""), 17).lower()
    board_name = clean_str(msg.get("team", ""), MAX_TEAM_LEN)
    app = "hello" if msg.get("app") == "hello" else "detector"
    with lock:
        name = names.get(mac) or board_name
    # Hello Board reports in before the team has typed a name; the detector always has one
    if len(mac) < 11 or (not name and app != "hello"):
        return

    now = time.time()
    person = bool(msg.get("person", False))
    with lock:
        t = teams.get(mac)
        is_new = t is None
        if is_new:
            t = {"mac": mac, "seen": 0, "person": False, "online": False,
                 "first_seen": now}
            teams[mac] = t
            first_online.append(mac)
        was_online, was_person, was_app, was_name = t["online"], t["person"], t.get("app"), t.get("name")
        t.update({
            "app": app,
            "name": name,
            "board_name": board_name,
            "fw": clean_str(msg.get("fw", ""), 8),
            "ip": clean_str(msg.get("ip", addr[0]), 15) or addr[0],
            "person": person,
            "score": to_int(msg.get("score"), 0, 0, 100),
            "seen_board": to_int(msg.get("seen"), 0, 0, 10**7),
            "infer_ms": to_int(msg.get("infer_ms"), 0, 0, 10**6),
            "uptime_s": to_int(msg.get("uptime_s"), 0, 0, 10**9),
            "rssi": to_int(msg.get("rssi"), 0, -127, 0),
            "chip": clean_str(msg.get("chip", ""), 16),
            "psram": bool(msg.get("psram", False)),
            "cam": bool(msg.get("cam", False)),
            "fps": round(float(msg.get("fps", 0) or 0), 1) if isinstance(msg.get("fps", 0), (int, float)) else 0,
            "online": True,
            "last": now,
        })
        # board details from Hello Board's report (the detector doesn't send them: keep the last ones)
        if "flash_mb" in msg:
            t.update({
                "psram_mb": to_int(msg.get("psram_mb"), 0, 0, 1024),
                "flash_mb": to_int(msg.get("flash_mb"), 0, 0, 1024),
                "camera": clean_str(msg.get("camera", ""), 15),
                "mic": bool(msg.get("mic", False)),
            })
        # "people seen" counts from the last reset (POST /reset_seen); a board restart starts it at 0 again
        if t["seen_board"] < t.get("seen_offset", 0):
            t["seen_offset"] = 0
        t["seen"] = t["seen_board"] - t.get("seen_offset", 0)
        t["label"] = display_name(mac, name)
        if is_new:
            add_event("online", f"{t['label']} joined (#{len(first_online)})")
            log(f"NEW TEAM #{len(first_online)}: {name!r} mac={mac} ip={t['ip']}")
        elif not was_online:
            add_event("online", f"{t['label']} is back online")
            log(f"back online: {name!r} ({mac})")
        if not is_new and not was_name and name:
            add_event("online", f"{display_name(mac, '')} is now team {t['label']}")
        if was_app and was_app != app and not is_new:
            add_event("online", f"{t['label']} is now running {'Hello Board' if app == 'hello' else 'Person Detector'}")
        if person and not was_person:
            add_event("person", f"{t['label']} spotted a person!")


def udp_listener(port):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    if hasattr(socket, "SO_REUSEPORT"):
        try:
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
        except OSError:
            pass
    sock.bind(("", port))
    log(f"listening for UDP packets on port {port}")
    while True:
        data, addr = sock.recvfrom(2048)
        handle_packet(data, addr)


def video_worker():
    """Ask every online camera board to push frames to us, and keep the newest per board."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("", 0))
    sock.settimeout(0.05)
    log(f"live video: requesting frames on UDP {VIDEO_PORT} (reply port {sock.getsockname()[1]})")
    last_ask = 0.0
    while True:
        now = time.time()
        if now - last_ask > 1.0:
            last_ask = now
            with lock:
                targets = [t["ip"] for t in teams.values() if t["online"] and t.get("cam")]
            for ip in targets:
                try:
                    sock.sendto(b"watch", (ip, VIDEO_PORT))
                except OSError:
                    pass
        try:
            data, addr = sock.recvfrom(2048)
        except (socket.timeout, OSError):
            continue
        if len(data) < 12 or data[:4] != b"WSF1" or data[8:10] != b"\xff\xd8":
            continue  # not a frame / not a JPEG
        seq = int.from_bytes(data[4:8], "big")
        with lock:
            mac = next((m for m, t in teams.items() if t["ip"] == addr[0]), None)
            if mac:
                frames[mac] = (seq, data[8:], time.time())


def offline_watcher():
    while True:
        time.sleep(1)
        now = time.time()
        with lock:
            for t in teams.values():
                if t["online"] and now - t["last"] > OFFLINE_AFTER_S:
                    t["online"] = False
                    t["person"] = False
                    add_event("offline", f"{t['label']} went offline")
                    log(f"offline: {t['name']!r} ({t['mac']})")


def snapshot():
    with lock:
        order = {mac: i + 1 for i, mac in enumerate(first_online)}
        rows = []
        for t in teams.values():
            r = dict(t)
            r["rank"] = order.get(t["mac"], 0)
            rows.append(r)
        return {"now": time.time(), "teams": rows, "events": list(events)}


# ---------------------------------------------------------------- demo mode

DEMO_NAMES = ["Rockets", "Byte Me", "Null Pointers", "Blinky Bois", "Overclocked",
              "Kernel Panic", "Segfaults", "TinyBrains", "Watchdogs", "PSRAMen",
              "Rockets", "Bit Flippers"]


def demo_feeder(port):
    """Sends realistic fake packets to ourselves over UDP, exercising the real path."""
    out = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    fakes = []
    for i, name in enumerate(DEMO_NAMES):
        # locally administered MAC prefix, so it can never clash with a real board
        mac = "02:de:" + ":".join("%02x" % random.randint(0, 255) for _ in range(4))
        fakes.append({"team": name, "mac": mac, "seen": 0, "person": False,
                      "start": time.time() + i * random.uniform(1, 3),
                      "infer": random.randint(70, 160), "rssi": random.randint(-75, -40),
                      "flaky": i == 7})
    log("demo mode: simulating %d teams" % len(fakes))
    while True:
        now = time.time()
        for f in fakes:
            if now < f["start"]:
                continue
            if f["flaky"] and int(now) % 40 < 8:
                continue  # drop out for a while to show the offline state
            if random.random() < 0.12:
                new = not f["person"]
                if new:
                    f["seen"] += 1
                f["person"] = new
            score = random.randint(65, 98) if f["person"] else random.randint(2, 40)
            pkt = {"v": 1, "team": f["team"], "mac": f["mac"], "ip": "127.0.0.1",
                   "person": f["person"], "score": score, "seen": f["seen"],
                   "infer_ms": f["infer"] + random.randint(-5, 5),
                   "uptime_s": int(now - f["start"]), "chip": "esp32s3",
                   "psram": True, "rssi": f["rssi"] + random.randint(-3, 3)}
            out.sendto(json.dumps(pkt).encode(), ("127.0.0.1", port))
        time.sleep(1.0)


# ---------------------------------------------------------------- http

class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        pass

    def do_POST(self):
        if self.path == "/reset_seen":
            with lock:
                for t in teams.values():
                    t["seen_offset"] = t.get("seen_board", 0)
                    t["seen"] = 0
                add_event("online", "People-seen counts reset")
            log("people-seen counts reset")
            self.send_response(204)
            self.end_headers()
            return
        if self.path != "/rename":
            self.send_error(404)
            return
        try:
            length = min(int(self.headers.get("Content-Length", 0)), 1024)
            req = json.loads(self.rfile.read(length) or b"{}")
            mac = clean_str(req.get("mac", ""), 17).lower()
            name = clean_str(req.get("name", ""), MAX_TEAM_LEN + 1)
        except (ValueError, AttributeError):
            self.send_error(400)
            return
        if name and not TEAM_NAME_RE.match(name):
            self.send_error(400, "Use 1-24 letters, digits, space, - _ .")
            return
        if not rename(mac, name):
            self.send_error(404, "unknown board")
            return
        self.send_response(204)
        self.end_headers()

    def do_GET(self):
        if self.path in ("/", "/index.html"):
            body = PAGE.encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        elif self.path == "/state":
            body = json.dumps(snapshot()).encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        elif self.path == "/video":
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.send_header("Cache-Control", "no-cache")
            self.end_headers()
            sent = {}
            try:
                while True:
                    with lock:
                        fresh = [(m, f) for m, f in frames.items() if sent.get(m) != f[0]]
                    for mac, (seq, jpg, _) in fresh:
                        sent[mac] = seq
                        payload = json.dumps({"mac": mac, "jpg": base64.b64encode(jpg).decode()})
                        self.wfile.write(f"data: {payload}\n\n".encode())
                    if fresh:
                        self.wfile.flush()
                    time.sleep(0.03)
            except (BrokenPipeError, ConnectionResetError, OSError):
                return
        elif self.path.startswith("/cam/") and self.path.endswith(".jpg"):
            mac = self.path[5:-4].lower()
            with lock:
                f = frames.get(mac)
            if not f:
                self.send_error(404)
                return
            self.send_response(200)
            self.send_header("Content-Type", "image/jpeg")
            self.send_header("Content-Length", str(len(f[1])))
            self.end_headers()
            self.wfile.write(f[1])
        elif self.path == "/events":
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.send_header("Cache-Control", "no-cache")
            self.send_header("Connection", "keep-alive")
            self.end_headers()
            try:
                while True:
                    payload = json.dumps(snapshot())
                    self.wfile.write(f"data: {payload}\n\n".encode())
                    self.wfile.flush()
                    time.sleep(0.5)
            except (BrokenPipeError, ConnectionResetError, OSError):
                return
        else:
            self.send_error(404)


def lan_ips():
    ips = set()
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("10.255.255.255", 1))  # no packet is sent
        ips.add(s.getsockname()[0])
        s.close()
    except OSError:
        pass
    try:
        for info in socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET):
            ips.add(info[4][0])
    except OSError:
        pass
    return sorted(ip for ip in ips if not ip.startswith("127."))


def main():
    load_names()
    ap = argparse.ArgumentParser(description="ESP32 workshop live dashboard")
    ap.add_argument("--udp-port", type=int, default=5005)
    ap.add_argument("--http-port", type=int, default=8080)
    ap.add_argument("--demo", action="store_true", help="simulate fake teams")
    ap.add_argument("--no-video", action="store_true", help="don't request live camera frames")
    args = ap.parse_args()

    threading.Thread(target=udp_listener, args=(args.udp_port,), daemon=True).start()
    threading.Thread(target=offline_watcher, daemon=True).start()
    if not args.no_video:
        threading.Thread(target=video_worker, daemon=True).start()
    if args.demo:
        threading.Thread(target=demo_feeder, args=(args.udp_port,), daemon=True).start()

    try:
        server = ThreadingHTTPServer(("", args.http_port), Handler)
    except OSError as e:
        raise SystemExit(f"HTTP port {args.http_port} is busy ({e}). Try --http-port 8090")
    server.daemon_threads = True
    print()
    print("  Dashboard:  http://127.0.0.1:%d" % args.http_port)
    for ip in lan_ips():
        print("              http://%s:%d   (this Mac on the LAN)" % (ip, args.http_port))
    print("  Boards must be on the same Wi-Fi and broadcast to UDP port %d." % args.udp_port)
    print("  If nothing shows up on macOS: System Settings > Privacy & Security >")
    print("  Local Network > allow Terminal / your editor.\n")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nbye")


PAGE = r"""<!doctype html>
<html lang="en"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Person Detector Live</title>
<style>
:root{--bg:#0b0f17;--panel:#131a26;--line:#243044;--text:#eef3fb;--dim:#8a99b3;
  --green:#22e584;--green-bg:#0d3b26;--red:#ff5c6c;--amber:#ffc24b;--blue:#4cc3ff}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--text);
  font:16px/1.3 -apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Helvetica,Arial,sans-serif}
header{display:flex;align-items:center;justify-content:space-between;gap:16px;
  padding:14px 20px;border-bottom:1px solid var(--line);flex-wrap:wrap}
h1{margin:0;font-size:clamp(22px,2.6vw,36px);letter-spacing:.3px}
.count{font-size:clamp(18px,2vw,28px);font-weight:700}
.count b{color:var(--green);font-size:1.3em}
.conn{font-size:13px;color:var(--dim)} .conn.bad{color:var(--red)}
main{display:grid;grid-template-columns:minmax(0,1fr) 300px;gap:16px;padding:16px 20px}
@media (max-width:1100px){main{grid-template-columns:1fr}}
#grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(210px,1fr));gap:12px;align-content:start}
.tile{background:var(--panel);border:2px solid var(--line);border-radius:14px;padding:12px 14px;
  transition:border-color .2s,background .2s,opacity .3s;min-width:0}
.tile.person{border-color:var(--green);background:linear-gradient(180deg,var(--green-bg),var(--panel) 75%)}
.tile.offline{opacity:.4}
.tile.pulse{animation:pulse 1s ease-out}
@keyframes pulse{0%{box-shadow:0 0 0 0 rgba(34,229,132,.85);transform:scale(1.04)}
  100%{box-shadow:0 0 0 22px rgba(34,229,132,0);transform:scale(1)}}
.top{display:flex;align-items:center;gap:8px}
.dot{width:12px;height:12px;border-radius:50%;background:var(--red);flex:none}
.online .dot{background:var(--green);box-shadow:0 0 8px var(--green)}
.name{font-weight:800;font-size:20px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.rank{margin-left:auto;color:var(--dim);font-size:13px;flex:none}
.fw{flex:none;font-size:12px;font-weight:800;padding:1px 6px;border-radius:6px;background:#1c2433;color:var(--dim)}
.fw.old{background:var(--amber);color:#111}
.edit{flex:none;background:none;border:1px solid var(--line);border-radius:8px;color:var(--dim);font-size:15px;padding:2px 7px;cursor:pointer}
.edit:hover{color:var(--text);border-color:var(--dim)}
.state{margin:10px 0 8px;font-size:26px;font-weight:900;letter-spacing:1px;color:var(--dim)}
.person .state{color:var(--green)}
.hello .state{color:var(--blue)} .hello.noname .state{color:var(--amber)}
.hello .stats b{font-size:15px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.bar{height:8px;background:#0a0e15;border-radius:6px;overflow:hidden;margin-bottom:10px}
.bar i{display:block;height:100%;background:var(--blue);transition:width .3s}
.person .bar i{background:var(--green)}
.stats{display:grid;grid-template-columns:repeat(3,1fr);gap:4px;font-size:12px;color:var(--dim)}
.stats b{display:block;color:var(--text);font-size:18px}
aside{display:flex;flex-direction:column;gap:12px;min-width:0}
.card{background:var(--panel);border:1px solid var(--line);border-radius:14px;padding:12px 14px}
.card h2{margin:0 0 8px;font-size:14px;text-transform:uppercase;letter-spacing:1.5px;color:var(--amber)}
ol{margin:0;padding-left:22px} li{margin:3px 0;font-size:17px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
li span{color:var(--dim);float:right;margin-left:8px}
#ticker{list-style:none;padding:0;max-height:260px;overflow:hidden}
#ticker li{font-size:14px;white-space:normal} #ticker .person{color:var(--green)}
#ticker .offline{color:var(--red)} #ticker .online{color:var(--blue)}
.left{display:flex;flex-direction:column;gap:16px;min-width:0}
#spot{display:flex;gap:18px;align-items:center;background:var(--panel);border:2px solid var(--line);
  border-radius:16px;padding:14px;flex-wrap:wrap}
#spot.person{border-color:var(--green);background:linear-gradient(90deg,var(--green-bg),var(--panel) 70%)}
#spot .view{width:min(320px,100%);aspect-ratio:1;background:#000;border-radius:10px;overflow:hidden;position:relative;flex:none}
#spot .view img,.cam img{width:100%;height:100%;object-fit:cover;image-rendering:pixelated;display:block}
.nocam{position:absolute;inset:0;display:flex;align-items:center;justify-content:center;color:var(--dim);font-size:14px;text-align:center;padding:8px}
.hasvideo .nocam{display:none}
#spot .meta{flex:1;min-width:220px}
#spot .who{font-size:clamp(28px,3.4vw,48px);font-weight:900;line-height:1.1;word-break:break-word}
#spot .big{font-size:clamp(28px,3vw,44px);font-weight:900;color:var(--dim);margin:8px 0}
#spot.person .big{color:var(--green)}
#spot .hint{color:var(--dim);font-size:14px;margin-top:10px}
#spot .label{color:var(--amber);font-size:13px;letter-spacing:1.5px;text-transform:uppercase}
.cam{position:relative;aspect-ratio:1;background:#000;border-radius:10px;overflow:hidden;margin:-2px -4px 10px}
.tile{cursor:pointer} .tile.pinned{outline:3px solid var(--amber);outline-offset:2px}
body.hellomode .det-only,body:not(.hellomode) .hello-only,body.hellomode #spot{display:none}
.checks{list-style:none;padding:0} .checks li span{color:var(--text);font-weight:800}
#empty{grid-column:1/-1;text-align:center;color:var(--dim);font-size:22px;padding:60px 10px}
</style></head><body>
<header>
  <h1 id="title">&#128075; Hello Board &mdash; Live</h1>
  <div class="count"><b id="n">0</b> teams online <span id="conn" class="conn">connecting…</span></div>
</header>
<main>
  <div class="left">
    <section id="spot" hidden>
      <div class="view"><img id="spot-img" alt=""><div class="nocam">waiting for video…</div></div>
      <div class="meta">
        <div class="label">&#127909; What the AI sees &mdash; 96&times;96 pixels</div>
        <div class="who" id="spot-who"></div>
        <div class="big" id="spot-state"></div>
        <div class="bar"><i id="spot-bar"></i></div>
        <div class="hint" id="spot-hint"></div>
      </div>
    </section>
    <section id="grid"><div id="empty">Waiting for boards… flash, set your team name, and watch this space.</div></section>
  </div>
  <aside>
    <div class="card hello-only"><h2>&#128269; Board check</h2><ul id="checks" class="checks"></ul></div>
    <div class="card det-only"><h2>&#127942; Most people seen</h2><ol id="lb-seen"></ol></div>
    <div class="card det-only"><h2>&#9889; Fastest inference</h2><ol id="lb-fast"></ol></div>
    <div class="card"><h2>&#129351; First online</h2><ol id="lb-first"></ol></div>
    <div class="card"><h2>&#128227; Live feed</h2><ul id="ticker"></ul></div>
  </aside>
</main>
<script>
const esc = s => String(s).replace(/[&<>"']/g, c => ({"&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;","'":"&#39;"}[c]));
const grid = document.getElementById("grid"), tiles = {}, lastPerson = {};
function li(text, right){ return `<li>${esc(text)}${right!==undefined?`<span>${esc(right)}</span>`:""}</li>`; }
function tile(t){
  let el = tiles[t.mac];
  if(!el){
    el = document.createElement("div"); el.className="tile"; tiles[t.mac]=el; grid.appendChild(el);
    el.innerHTML = `<div class="cam"><img alt=""><div class="nocam">no video</div></div><div class="info"></div>`;
    el.onclick = () => { pinned = (pinned===t.mac) ? null : t.mac; if(pinned) setSpot(pinned); render(lastState); };
  }
  el.classList.toggle("online", t.online); el.classList.toggle("offline", !t.online);
  el.classList.toggle("person", t.online && t.person);
  if(t.online && t.person && lastPerson[t.mac]===false){
    el.classList.remove("pulse"); void el.offsetWidth; el.classList.add("pulse");
    // auto-spotlight the team that just spotted someone (unless one is pinned)
    if(!pinned && (Date.now()-spotSince > 4000 || !(teamsBy[spot]||{}).person)) setSpot(t.mac);
  }
  el.classList.toggle("pinned", pinned===t.mac);
  el.querySelector(".cam").style.display = t.cam ? "" : "none";
  lastPerson[t.mac] = !!(t.online && t.person);
  const hello = t.app === "hello";
  el.classList.toggle("hello", hello); el.classList.toggle("noname", hello && !t.name);
  if(hello){
    // Hello Board: show what the board report found, so we can see everyone's hardware
    el.querySelector(".info").innerHTML = `<div class="top"><span class="dot"></span><span class="name" title="${esc(t.label)}">${esc(t.label)}</span><span class="rank">#${t.rank}</span>${fwChip(t)}<button class="edit" title="Rename team" data-mac="${esc(t.mac)}">&#9998;</button></div>
    <div class="state">${!t.online?"OFFLINE":t.name?"HELLO!":"SET TEAM NAME"}</div>
    <div class="stats"><div><b>${t.psram_mb?t.psram_mb+" MB":"none"}</b>PSRAM</div><div><b>${esc(t.camera||"none")}</b>camera</div><div><b>${t.mic?"yes":"no"}</b>mic</div></div>
    <div class="stats" style="margin-top:6px"><div><b>${t.flash_mb} MB</b>flash</div><div><b>${esc(t.chip)}</b>chip</div><div><b>${t.rssi}</b>dBm</div></div>`;
    return;
  }
  el.querySelector(".info").innerHTML = `<div class="top"><span class="dot"></span><span class="name" title="${esc(t.label)}">${esc(t.label)}</span><span class="rank">#${t.rank}</span>${fwChip(t)}<button class="edit" title="Rename team" data-mac="${esc(t.mac)}">&#9998;</button></div>
    <div class="state">${!t.online?"OFFLINE":t.person?"PERSON":"no person"}</div>
    <div class="bar"><i style="width:${t.online?t.score:0}%"></i></div>
    <div class="stats"><div><b>${t.seen}</b>seen</div><div><b>${t.infer_ms}</b>ms</div><div><b>${t.rssi}</b>dBm</div></div>`;
}
// Firmware version on each tile. Person Detector boards older than LATEST_DETECTOR get flagged.
const LATEST_DETECTOR = "v2";
function fwChip(t){
  if(t.app === "hello") return `<span class="fw">${esc(t.fw || "v1")}</span>`;
  const old = t.fw !== LATEST_DETECTOR;
  return `<span class="fw${old ? " old" : ""}" title="${old ? "Old firmware: flash 2-Person-Detector-" + LATEST_DETECTOR : "Latest firmware"}">${old ? "OLD FW" : esc(t.fw)}</span>`;
}
// ✎ on a tile: set the team name here (works for boards whose console can't be reached)
grid.addEventListener("click", async e => {
  const b = e.target.closest(".edit");
  if(!b) return;
  e.stopPropagation();
  const t = teamsBy[b.dataset.mac] || {};
  const name = prompt("Team name (letters, digits, space, - _ .)", t.name || "");
  if(name === null) return;
  const r = await fetch("/rename", {method:"POST", headers:{"Content-Type":"application/json"}, body: JSON.stringify({mac: b.dataset.mac, name: name.trim()})});
  if(!r.ok) alert("Not saved: use up to 24 letters, digits, space, - _ .");
}, true);
let pinned = null, spot = null, spotSince = 0, lastState = {teams:[],events:[]}, teamsBy = {};
const frameAt = {};
function setSpot(mac){ if(spot!==mac){ spot = mac; spotSince = Date.now(); document.getElementById("spot-img").removeAttribute("src"); } }
function renderSpot(){
  const box = document.getElementById("spot");
  if(!spot || !teamsBy[spot]){
    const cand = Object.values(teamsBy).find(t => t.online && t.cam && t.app !== "hello");
    if(cand) setSpot(cand.mac);
  }
  let t = teamsBy[spot];
  if(t && (t.app === "hello" || !t.cam)) t = null;   // Hello Board has no video
  box.hidden = !t;
  if(!t) return;
  box.classList.toggle("person", t.online && t.person);
  document.getElementById("spot-who").textContent = t.label;
  document.getElementById("spot-state").textContent = !t.online ? "OFFLINE" : t.person ? `PERSON ${t.score}%` : `no person ${t.score}%`;
  document.getElementById("spot-bar").style.width = (t.online ? t.score : 0) + "%";
  document.getElementById("spot-hint").textContent = pinned ? "📌 Pinned. Click the tile again to go back to auto." :
    "Auto: follows the latest team to spot a person. Click any tile to pin it here.";
  box.querySelector(".view").classList.toggle("hasvideo", Date.now() - (frameAt[spot]||0) < 3000);
}
function render(s){
  lastState = s;
  teamsBy = Object.fromEntries(s.teams.map(t => [t.mac, t]));
  const ts = s.teams.slice().sort((a,b)=>a.rank-b.rank);
  document.getElementById("empty").style.display = ts.length ? "none" : "";
  ts.forEach(tile);
  // Hello Board mode until any board runs the Person Detector
  const on = ts.filter(t => t.online), hello = !on.some(t => t.app !== "hello");
  document.body.classList.toggle("hellomode", hello);
  document.getElementById("title").innerHTML = hello ? "&#128075; Hello Board &mdash; Live" : "&#128065; TinyML Person Detector &mdash; Live";
  const hb = on.filter(t => t.app === "hello"), n = hb.length;
  const cnt = f => `${hb.filter(f).length} / ${n}`;
  document.getElementById("checks").innerHTML = n ? [
    li("Team name set", cnt(t => t.name)), li("Camera found", cnt(t => t.camera)),
    li("PSRAM", cnt(t => t.psram_mb)), li("Microphone", cnt(t => t.mic))].join("") : li("waiting for boards…");
  document.getElementById("n").textContent = ts.filter(t=>t.online).length;
  document.getElementById("lb-seen").innerHTML = ts.slice().sort((a,b)=>b.seen-a.seen).slice(0,5).map(t=>li(t.label,t.seen)).join("");
  document.getElementById("lb-fast").innerHTML = ts.filter(t=>t.online&&t.infer_ms>0).sort((a,b)=>a.infer_ms-b.infer_ms).slice(0,5).map(t=>li(t.label,t.infer_ms+" ms")).join("");
  document.getElementById("lb-first").innerHTML = ts.slice(0,5).map(t=>li(t.label)).join("");
  document.getElementById("ticker").innerHTML = s.events.slice().reverse().slice(0,12).map(e=>{
    const tm = new Date(e.t*1000).toLocaleTimeString([], {hour:"2-digit",minute:"2-digit",second:"2-digit"});
    return `<li class="${esc(e.kind)}">${tm} &nbsp;${esc(e.text)}</li>`;}).join("");
  renderSpot();
  for(const [mac, el] of Object.entries(tiles))
    el.querySelector(".cam").classList.toggle("hasvideo", Date.now() - (frameAt[mac]||0) < 3000);
}
const conn = document.getElementById("conn");
function connect(){
  const es = new EventSource("/events");
  es.onopen = () => { conn.textContent = "● live"; conn.className = "conn"; };
  es.onmessage = e => render(JSON.parse(e.data));
  es.onerror = () => { conn.textContent = "reconnecting…"; conn.className = "conn bad"; };
}
connect();
function connectVideo(){
  const vs = new EventSource("/video");
  vs.onmessage = e => {
    const f = JSON.parse(e.data), url = "data:image/jpeg;base64," + f.jpg;
    frameAt[f.mac] = Date.now();
    const el = tiles[f.mac];
    if(el){ el.querySelector(".cam img").src = url; el.querySelector(".cam").classList.add("hasvideo"); }
    if(f.mac === spot){ document.getElementById("spot-img").src = url; document.querySelector("#spot .view").classList.add("hasvideo"); }
  };
}
connectVideo();
</script></body></html>
"""

if __name__ == "__main__":
    main()
