#!/usr/bin/env python3
"""Talk to desk buddy over USB serial.

  buddy.py status              what it feels and senses right now (JSON)
  buddy.py mood NAME           show a mood now: idle bored asleep happy excited dizzy angry surprised curious loved
  buddy.py gallery DIR         screenshot every mood to DIR/mood-<name>.png

Keeps the claude-cube bridge paused (~/.claude-cube/paused) so it does not write into this firmware.
"""
from __future__ import annotations

import json
import os
import select
import sys
import termios
import time
import tty
from glob import glob
from pathlib import Path
from typing import Callable, Optional

REPO = Path(__file__).resolve().parents[2]
PAUSE_PATH = Path.home() / ".claude-cube" / "paused"
MOODS = ["idle", "bored", "asleep", "happy", "excited", "dizzy", "angry", "surprised", "curious", "loved"]
READ_TIMEOUT_S = 1.5
ATTEMPTS = 3  # the board occasionally drops a command while it is busy pushing a frame
SETTLE_S = 0.8  # let the eyes ease into the new pose before the screenshot


def find_port() -> Optional[str]:
    explicit = os.environ.get("CLAUDE_CUBE_PORT")
    if explicit:
        return explicit
    ports = sorted(glob("/dev/cu.usbmodem*"))
    return ports[0] if ports else None


def open_raw(port: str) -> int:
    fd = os.open(port, os.O_RDWR | os.O_NOCTTY)
    tty.setraw(fd)
    attrs = termios.tcgetattr(fd)
    attrs[2] = (attrs[2] | termios.CLOCAL) & ~termios.HUPCL  # keep DTR up on close: no reset
    termios.tcsetattr(fd, termios.TCSANOW, attrs)
    termios.tcflush(fd, termios.TCIFLUSH)
    return fd


def read_until(fd: int, done: Callable[[bytearray], bool]) -> bytearray:
    buf = bytearray()
    deadline = time.monotonic() + READ_TIMEOUT_S
    while not done(buf):
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError("no answer from desk buddy; is its firmware flashed?")
        if select.select([fd], [], [], remaining)[0]:
            buf += os.read(fd, 65536)
    return buf


def line_after(buf: bytearray, prefix: bytes) -> Optional[bytes]:
    start = buf.find(prefix)
    end = buf.find(b"\n", start) if start >= 0 else -1
    return bytes(buf[start + len(prefix):end]) if end >= 0 else None


def ask(fd: int, command: bytes, prefix: bytes) -> bytearray:
    """Send `command` and wait for its reply line, retrying if the board dropped the command."""
    for attempt in range(ATTEMPTS):
        termios.tcflush(fd, termios.TCIFLUSH)
        os.write(fd, b"\n" + command + b"\n")
        try:
            return read_until(fd, lambda b: line_after(b, prefix) is not None)
        except TimeoutError:
            if attempt == ATTEMPTS - 1:
                raise
    raise AssertionError("unreachable")


def status(fd: int) -> dict:
    return json.loads(line_after(ask(fd, b"status", b"STATUS "), b"STATUS "))


def snap(fd: int) -> bytes:
    sys.path.insert(0, str(REPO / "claude-cube" / "host"))  # reuse claude-cube's tested PNG encoder
    from claudecube.snapshot import rgb565_to_png

    buf = ask(fd, b"snap", b"SNAP ")
    header = line_after(buf, b"SNAP ")
    width, height, _ = (int(v) for v in header.split())
    start = buf.find(b"SNAP ") + len(b"SNAP ") + len(header) + 1
    size = width * height * 2
    pixels = bytearray(buf[start:])
    if len(pixels) < size:
        pixels += read_until(fd, lambda b: len(b) >= size - (len(buf) - start))
    return rgb565_to_png(bytes(pixels[:size]), width, height)


def gallery(fd: int, out_dir: Path) -> None:
    out_dir.mkdir(parents=True, exist_ok=True)
    for name in MOODS:
        os.write(fd, f"\nmood {name}\n".encode())
        time.sleep(SETTLE_S)
        path = out_dir / f"mood-{name}.png"
        path.write_bytes(snap(fd))
        print(path)
    os.write(fd, b"\nmood idle\n")


def main(argv: list) -> int:
    if not argv or argv[0] not in ("status", "mood", "gallery") or (argv[0] != "status" and len(argv) < 2):
        print(__doc__, file=sys.stderr)
        return 2
    port = find_port()
    if port is None:
        print("No cube found on /dev/cu.usbmodem*", file=sys.stderr)
        return 2
    PAUSE_PATH.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
    PAUSE_PATH.touch()
    time.sleep(1.2)  # let a running bridge let go of the port
    fd = open_raw(port)
    try:
        if argv[0] == "status":
            print(json.dumps(status(fd), indent=2))
        elif argv[0] == "mood":
            if argv[1] not in MOODS:
                print(f"unknown mood {argv[1]!r}; one of: {' '.join(MOODS)}", file=sys.stderr)
                return 2
            os.write(fd, f"\nmood {argv[1]}\n".encode())
        else:
            gallery(fd, Path(argv[1]))
    finally:
        os.close(fd)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
