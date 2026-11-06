#!/usr/bin/env python3
"""Save what the cube's screen is showing as a PNG.

  snap.py [out.png]       the current page; default ~/.claude-cube/snap.png
  snap.py --all [dir]     every page, as snap-tokens.png ... snap-cost.png; default ~/.claude-cube/

Pauses the bridge while it holds the serial port, the same way firmware/flash.sh does.
"""
from __future__ import annotations

import os
import select
import sys
import termios
import time
import tty
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from claudecube.serial_link import find_port  # noqa: E402
from claudecube.snapshot import rgb565_to_png  # noqa: E402

STATE_DIR = Path.home() / ".claude-cube"
PAUSE_PATH = STATE_DIR / "paused"
DEFAULT_OUT = STATE_DIR / "snap.png"
BRIDGE_RELEASE_S = 1.2
READ_TIMEOUT_S = 5.0
HEADER_PREFIX = b"SNAP "
PAGE_NAMES = ["tokens", "model", "context", "now", "cost"]  # order of Page in firmware/src/pages.h


def open_raw(port: str) -> int:
    fd = os.open(port, os.O_RDWR | os.O_NOCTTY)
    tty.setraw(fd)
    attrs = termios.tcgetattr(fd)
    attrs[2] = (attrs[2] | termios.CLOCAL) & ~termios.HUPCL
    termios.tcsetattr(fd, termios.TCSANOW, attrs)
    termios.tcflush(fd, termios.TCIFLUSH)
    return fd


def read_until(fd: int, buf: bytearray, done, deadline: float) -> None:
    while not done(buf):
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError("the cube did not answer; is the claude-cube firmware flashed?")
        ready, _, _ = select.select([fd], [], [], remaining)
        if ready:
            buf += os.read(fd, 65536)


def capture(fd: int, command: bytes) -> tuple[int, bytes]:
    """Send `command` (ending in a snap request) and return (page index, PNG bytes)."""
    os.write(fd, command)
    deadline = time.monotonic() + READ_TIMEOUT_S
    buf = bytearray()
    read_until(fd, buf, lambda b: HEADER_PREFIX in b and b"\n" in b[b.find(HEADER_PREFIX):], deadline)
    start = buf.find(HEADER_PREFIX)
    end = buf.find(b"\n", start)
    width, height, page = (int(v) for v in buf[start + len(HEADER_PREFIX):end].split())
    pixels = bytearray(buf[end + 1:])
    size = width * height * 2
    read_until(fd, pixels, lambda b: len(b) >= size, deadline)
    return page, rgb565_to_png(bytes(pixels[:size]), width, height)


def capture_all(fd: int, out_dir: Path) -> list:
    """Flip through every page once, starting from whichever page the cube is on."""
    saved = []
    command = b"\nsnap\n"
    for _ in PAGE_NAMES:
        page, png = capture(fd, command)
        path = out_dir / f"snap-{PAGE_NAMES[page]}.png"
        path.write_bytes(png)
        saved.append(path)
        command = b"next\nsnap\n"
    return saved


def main(argv: list) -> int:
    take_all = "--all" in argv
    args = [a for a in argv if a != "--all"]
    target = Path(args[0]).expanduser() if args else (STATE_DIR if take_all else DEFAULT_OUT)
    port = find_port()
    if port is None:
        print("No cube found on /dev/cu.usbmodem*", file=sys.stderr)
        return 1
    STATE_DIR.mkdir(mode=0o700, parents=True, exist_ok=True)
    (target if take_all else target.parent).mkdir(parents=True, exist_ok=True)
    PAUSE_PATH.touch()
    try:
        time.sleep(BRIDGE_RELEASE_S)
        fd = open_raw(port)
        try:
            if take_all:
                saved = capture_all(fd, target)
            else:
                target.write_bytes(capture(fd, b"\nsnap\n")[1])
                saved = [target]
        finally:
            os.close(fd)
    finally:
        PAUSE_PATH.unlink(missing_ok=True)
    for path in saved:
        print(path)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
