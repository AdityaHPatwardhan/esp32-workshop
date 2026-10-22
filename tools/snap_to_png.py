#!/usr/bin/env python3
"""See what the model sees: grab the 96x96 frame from the person detector and save a PNG.

    python snap_to_png.py /dev/cu.usbmodem1101 snap.png     # asks the board (needs pyserial)
    python snap_to_png.py --log console.txt snap.png        # or parse a saved console log
"""
import argparse
import re
import struct
import sys
import time
import zlib

W = H = 96
SCALE = 4


def parse(text):
    m = re.search(r"-----BEGIN FRAME 96x96-----\s*(.*?)\s*-----END FRAME-----", text, re.S)
    if not m:
        return None
    rows = [r.strip() for r in m.group(1).splitlines() if re.fullmatch(r"[0-9a-fA-F]{192}", r.strip())]
    if len(rows) != H:
        return None
    return [bytes.fromhex(r) for r in rows]


def from_port(port):
    import serial  # pyserial
    s = serial.Serial(port, 115200, timeout=0.2)
    s.reset_input_buffer()
    s.write(b"snap\r\n")
    buf = ""
    deadline = time.time() + 5
    while time.time() < deadline:
        buf += s.read(8192).decode("utf-8", "replace")
        if "-----END FRAME-----" in buf:
            break
    s.close()
    return parse(buf)


def write_png(path, rows):
    raw = b""
    for r in rows:
        line = bytes(p for p in r for _ in range(SCALE))
        raw += (b"\x00" + line) * SCALE

    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", W * SCALE, H * SCALE, 8, 0, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9))
    png += chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("port", nargs="?", help="serial port of the board")
    ap.add_argument("out", help="output PNG path")
    ap.add_argument("--log", help="parse this console log instead of talking to the board")
    a = ap.parse_args()

    if a.log:
        rows = parse(open(a.log, encoding="utf-8", errors="replace").read())
    elif a.port:
        rows = from_port(a.port)
    else:
        sys.exit("give a serial port or --log FILE")
    if not rows:
        sys.exit("no frame found (is the person detector running? try again)")
    write_png(a.out, rows)
    print(f"saved {a.out} ({W}x{H} upscaled x{SCALE})")


if __name__ == "__main__":
    main()
