#!/usr/bin/env bash
# Build all workshop firmwares and collect them in ../binaries/
#   binaries/<app>/{bootloader,partition-table,app}.bin   -> for flash.py
#   binaries/<app>-merged.bin                             -> for ESP Launchpad (flash at 0x0)
# Usage:  source ~/esp/esp-idf/export.sh && ./tools/build_all.sh
#         BOARD=s3eye ./tools/build_all.sh    (ESP32-S3-EYE: LED GPIO3, native-USB console)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BOARD="${BOARD:-}"
OUT="$ROOT/binaries${BOARD:+-$BOARD}"
mkdir -p "$OUT"

command -v idf.py >/dev/null || { echo "Run: source ~/esp/esp-idf/export.sh"; exit 1; }

# name  project-dir  build-dir  extra-defaults
build() {
    local name="$1" dir="$ROOT/firmware/$2" bdir="$3" extra="${4:-}"
    echo "=== $name ==="
    if [[ -n "$BOARD" ]]; then
        bdir="build-$BOARD"
        extra="sdkconfig.board.$BOARD"
    fi
    local args=(-C "$dir" -B "$dir/$bdir")
    if [[ -n "$extra" ]]; then
        args+=(-DSDKCONFIG="$dir/$bdir/sdkconfig" -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;$extra")
    fi
    idf.py "${args[@]}" set-target esp32s3 >/dev/null
    idf.py "${args[@]}" build merge-bin >/dev/null

    mkdir -p "$OUT/$name"
    cp "$dir/$bdir/bootloader/bootloader.bin"           "$OUT/$name/bootloader.bin"
    cp "$dir/$bdir/partition_table/partition-table.bin" "$OUT/$name/partition-table.bin"
    cp "$dir/$bdir/$2.bin"                              "$OUT/$name/app.bin"
    cp "$dir/$bdir/merged-binary.bin"                   "$OUT/$name-merged.bin"
    echo "    -> $(du -h "$OUT/$name-merged.bin" | cut -f1) $OUT/$name-merged.bin"
}

build 01_blink                       01_blink            build
build 03_person_detector             03_person_detector  build
# the board profile already picks the right console, so the usbconsole variant is generic-board only
[[ -z "$BOARD" ]] && build 03_person_detector-usbconsole  03_person_detector  build-usbconsole  sdkconfig.usbconsole

echo "Done. Binaries in $OUT"
