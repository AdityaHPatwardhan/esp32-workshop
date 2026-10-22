#!/usr/bin/env python3
"""Flash a workshop app plus a per-team settings (NVS) partition in one go.

    python flash.py --team "Rockets"
    python flash.py --team "Rockets" --port /dev/cu.usbmodem1101
    python flash.py --team "Rockets" --ssid WorkshopWiFi --password secret123

Needs:  pip install esptool esp-idf-nvs-partition-gen
"""

import argparse
import os
import subprocess
import sys
import tempfile
from importlib import metadata

CHIP = "esp32s3"
NVS_OFFSET = "0x9000"
NVS_SIZE = "0x6000"
NVS_NAMESPACE = "workshop"
PARTS = [("0x0", "bootloader.bin"), ("0x8000", "partition-table.bin"), ("0x10000", "app.bin")]
HERE = os.path.dirname(os.path.abspath(__file__))
INSTALL_HINT = "pip install esptool esp-idf-nvs-partition-gen"


def fail(msg):
    print(f"\nERROR: {msg}", file=sys.stderr)
    sys.exit(1)


def check_value(label, value, max_len, min_len=1):
    if not (min_len <= len(value) <= max_len):
        fail(f"{label} must be {min_len}-{max_len} characters (got {len(value)}).")
    bad = [c for c in value if not c.isprintable() or c in ',"\'\\' or ord(c) > 126]
    if bad:
        fail(f"{label} contains characters that aren't allowed: {''.join(sorted(set(bad)))!r} "
             "(use plain letters, digits, spaces, - and _).")


def module_version(dist):
    try:
        return metadata.version(dist)
    except metadata.PackageNotFoundError:
        return None


def esptool_cmd(*args):
    """esptool v5 renamed write_flash -> write-flash; v4 only knows the underscore form."""
    ver = module_version("esptool")
    major = int(ver.split(".")[0]) if ver else 0
    fixed = [a.replace("_", "-") if major >= 5 and a in ("write_flash", "erase_flash") else a
             for a in args]
    return [sys.executable, "-m", "esptool", *fixed]


def run(cmd):
    print("\n$ " + " ".join(cmd), flush=True)
    result = subprocess.run(cmd)
    if result.returncode != 0:
        fail("command failed (see output above). Check the USB cable/port; if it keeps "
             "failing, hold BOOT, tap RESET, release BOOT and try again.")


def main():
    ap = argparse.ArgumentParser(description="Flash a workshop app with your team name.")
    ap.add_argument("--team", required=True, help='team name, e.g. "Rockets" (max 24 chars)')
    ap.add_argument("--port", help="serial port (auto-detected if omitted)")
    ap.add_argument("--ssid", help="Wi-Fi network name (default: the one built into the firmware)")
    ap.add_argument("--password", default="", help="Wi-Fi password")
    ap.add_argument("--app", default="03_person_detector",
                    help="app folder under --bins (default: 03_person_detector)")
    ap.add_argument("--bins", default=os.path.join(HERE, "..", "binaries"),
                    help="binaries directory (default: ../binaries)")
    ap.add_argument("--erase", action="store_true", help="erase the whole flash first")
    ap.add_argument("--baud", default="460800", help="flashing baud rate (default 460800)")
    args = ap.parse_args()

    team = args.team.strip()
    check_value("Team name", team, 24)
    if args.ssid is not None:
        check_value("SSID", args.ssid, 32)
    if args.password:
        check_value("Password", args.password, 63, min_len=8)

    missing = [d for d in ("esptool", "esp-idf-nvs-partition-gen") if not module_version(d)]
    if missing:
        fail(f"missing Python package(s): {', '.join(missing)}\n       Install with:  {INSTALL_HINT}")

    app_dir = os.path.abspath(os.path.join(args.bins, args.app))
    for _, name in PARTS:
        if not os.path.isfile(os.path.join(app_dir, name)):
            fail(f"{name} not found in {app_dir}. Did you download the binaries folder?")

    with tempfile.TemporaryDirectory() as tmp:
        csv_path = os.path.join(tmp, "nvs.csv")
        bin_path = os.path.join(tmp, "nvs.bin")
        rows = ["key,type,encoding,value", f"{NVS_NAMESPACE},namespace,,", f"team,data,string,{team}"]
        if args.ssid is not None:
            rows.append(f"ssid,data,string,{args.ssid}")
            rows.append(f"pass,data,string,{args.password}")
        with open(csv_path, "w", newline="\n") as f:
            f.write("\n".join(rows) + "\n")

        run([sys.executable, "-m", "esp_idf_nvs_partition_gen", "generate",
             csv_path, bin_path, NVS_SIZE])

        base = ["--chip", CHIP, "--baud", args.baud]
        if args.port:
            base += ["--port", args.port]
        if args.erase:
            run(esptool_cmd(*base, "erase_flash"))

        flash_args = []
        for offset, name in PARTS:
            flash_args += [offset, os.path.join(app_dir, name)]
        flash_args += [NVS_OFFSET, bin_path]
        run(esptool_cmd(*base, "write_flash", *flash_args))

    print(f"\nDone! Flashed {args.app} for team {team!r}. Press RESET on the board if it "
          "doesn't restart by itself, then watch for your team on the projector.")


if __name__ == "__main__":
    main()
