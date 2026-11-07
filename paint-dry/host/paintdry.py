#!/usr/bin/env python3
"""Talk to Watching Paint Dry over USB serial.

  paintdry.py status              coat, colour, stage, how dry, how long left (JSON)
  paintdry.py snap FILE           screenshot the screen to FILE (PNG)
  paintdry.py at MINUTES          jump to that point of this coat (saved on the board)
  paintdry.py speed N             dry N times faster than real time (1 is real time; resets on reboot)
  paintdry.py touch X Y           press a finger on the wall at pixel X, Y
  paintdry.py coat N              start coat N, freshly painted
  paintdry.py reset               back to a freshly painted first coat, in real time
  paintdry.py gallery DIR         screenshot each stage of a coat to DIR/paint-<stage>.png, then reset
                                  (the touchscreen is ignored meanwhile, so only the script touches)

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
READ_TIMEOUT_S = 1.5
ATTEMPTS = 3  # the board occasionally drops a command while it is busy pushing a frame
SETTLE_S = 0.5
COMMANDS = {  # name: how many arguments it takes
    "status": 0, "snap": 1, "at": 1, "speed": 1, "touch": 2, "coat": 1, "reset": 0, "gallery": 1,
}

# (screenshot name, commands to send first)
GALLERY = [
    ("just-painted", ["untouchable 30000", "reset", "at 0"]),
    ("fingerprints", ["at 3", "touch 70 120", "touch 150 96"]),
    ("dripping", ["at 18"]),
    ("tacky", ["at 50"]),
    ("touch-dry", ["at 130"]),
    ("dry", ["at 180"]),
    ("another-coat", ["touch 160 120", "at 6"]),
]


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
            raise TimeoutError("no answer from the wall; is its firmware flashed?")
        if select.select([fd], [], [], remaining)[0]:
            buf += os.read(fd, 65536)
    return buf


def line_after(buf: bytearray, prefix: bytes) -> Optional[bytes]:
    start = buf.find(prefix)
    end = buf.find(b"\n", start) if start >= 0 else -1
    return bytes(buf[start + len(prefix):end]) if end >= 0 else None


def send(fd: int, command: str) -> None:
    os.write(fd, f"\n{command}\n".encode())
    time.sleep(0.15)  # one frame, so commands sent back to back are not dropped


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
    for name, commands in GALLERY:
        for command in commands:
            send(fd, command)
        time.sleep(SETTLE_S)
        s = status(fd)
        path = out_dir / f"paint-{name}.png"
        path.write_bytes(snap(fd))
        print(f"{path}  (coat {s['coat']}, {s['stage']}, {s['dryness']:.0%})")
    send(fd, "reset")
    send(fd, "untouchable 0")


def run(fd: int, name: str, args: list) -> int:
    if name == "status":
        print(json.dumps(status(fd), indent=2))
    elif name == "snap":
        Path(args[0]).write_bytes(snap(fd))
        print(args[0])
    elif name == "gallery":
        gallery(fd, Path(args[0]))
    else:
        send(fd, " ".join([name] + args))
    return 0


def main(argv: list) -> int:
    if not argv or argv[0] not in COMMANDS or len(argv) - 1 != COMMANDS[argv[0]]:
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
        return run(fd, argv[0], argv[1:])
    finally:
        os.close(fd)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
