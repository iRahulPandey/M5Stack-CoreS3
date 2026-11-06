#!/usr/bin/env bash
# Build with PlatformIO, then flash with esptool v5 (PlatformIO's bundled esptool 4.5.1 drops the
# CoreS3's native USB). Leaves the claude-cube bridge paused on purpose: while this firmware is on the
# board the bridge must not write to the port. claude-cube/firmware/flash.sh lifts the pause.
set -euo pipefail

STATE_DIR="$HOME/.claude-cube"
BUILD=".pio/build/cores3"
BOOT_APP0="$HOME/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin"

cd "$(dirname "$0")"
uvx platformio run

PORT="${CLAUDE_CUBE_PORT:-$(ls /dev/cu.usbmodem* 2>/dev/null | head -n 1)}"
if [[ -z "$PORT" ]]; then
    echo "No cube found on /dev/cu.usbmodem*" >&2
    exit 1
fi

mkdir -p -m 700 "$STATE_DIR"
touch "$STATE_DIR/paused"
sleep 2  # a few bridge ticks to close the port

uvx esptool --port "$PORT" write-flash \
    0x0 "$BUILD/bootloader.bin" \
    0x8000 "$BUILD/partitions.bin" \
    0xe000 "$BOOT_APP0" \
    0x10000 "$BUILD/firmware.bin"

echo "The claude-cube bridge stays paused while desk buddy is on the board."
echo "Run claude-cube/firmware/flash.sh to put the cube back."
