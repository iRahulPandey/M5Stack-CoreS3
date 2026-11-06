#!/usr/bin/env bash
# Build with PlatformIO, then flash with esptool v5. PlatformIO's bundled esptool 4.5.1 loses the
# connection to the CoreS3's native USB right after its stub starts; v5 does not.
# Pauses the bridge first so it is not writing to the port mid-upload.
set -euo pipefail

STATE_DIR="$HOME/.claude-cube"
PAUSE_FILE="$STATE_DIR/paused"
BUILD=".pio/build/cores3"
BOOT_APP0="$HOME/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin"

cd "$(dirname "$0")"
uvx platformio run

# Same lookup as the bridge: the first USB serial port with Espressif's vendor id, or CLAUDE_CUBE_PORT.
PORT="$(cd ../host && /usr/bin/python3 -m claudecube.serial_link || true)"
if [[ -z "$PORT" ]]; then
    echo "No cube found: no Espressif USB serial port (set CLAUDE_CUBE_PORT to choose one)" >&2
    exit 1
fi

mkdir -p -m 700 "$STATE_DIR"
touch "$PAUSE_FILE"
trap 'rm -f "$PAUSE_FILE"' EXIT
sleep 2  # a few bridge ticks to close the port

uvx esptool --port "$PORT" write-flash \
    0x0 "$BUILD/bootloader.bin" \
    0x8000 "$BUILD/partitions.bin" \
    0xe000 "$BOOT_APP0" \
    0x10000 "$BUILD/firmware.bin"
