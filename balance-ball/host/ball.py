#!/usr/bin/env python3
"""Talk to the Balance Ball over USB serial.

  ball.py status               where the ball is, how it feels, and what the board reads (JSON)
  ball.py level                call the way the board is held now level, and put the ball back in the middle
  ball.py tilt X Y [MS]        lean it from the Mac: pull in g, x to the right and y down (default 3 s)
  ball.py place X Y [MS]       hold the ball still at pixel X, Y (default 5 s)
  ball.py reset                put the ball back in the middle
  ball.py snap FILE            screenshot the screen to FILE (PNG)
  ball.py gallery DIR          screenshot every mood to DIR/mood-<name>.png

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
MOODS = ["blissful", "happy", "content", "worried", "panicking", "angry", "fuming", "sulking"]
READ_TIMEOUT_S = 1.5
ATTEMPTS = 3  # the board occasionally drops a command while it is busy pushing a frame
HOLD_MS = 9000

MOOD_TIMEOUT_S = 8.0
POLL_S = 0.2
SETTLE_S = 0.35  # let the red finish coming up before the screenshot

# Each mood and where to hold the ball for it; the gallery waits until the ball really feels it.
SHOTS = [
    ("happy", (160, 120)),
    ("blissful", (160, 120)),
    ("content", (205, 128)),
    ("worried", (250, 120)),
    ("panicking", (279, 96)),
    ("angry", (286, 120)),
    ("fuming", (286, 120)),
    ("sulking", (160, 120)),
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
            raise TimeoutError("no answer from the Balance Ball; is its firmware flashed?")
        if select.select([fd], [], [], remaining)[0]:
            buf += os.read(fd, 65536)
    return buf


def line_after(buf: bytearray, prefix: bytes) -> Optional[bytes]:
    start = buf.find(prefix)
    end = buf.find(b"\n", start) if start >= 0 else -1
    return bytes(buf[start + len(prefix):end]) if end >= 0 else None


def send(fd: int, command: str) -> None:
    os.write(fd, f"\n{command}\n".encode())


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


def snap(fd: int) -> tuple:
    """A PNG of the screen, and the mood the ball was in when it was drawn."""
    sys.path.insert(0, str(REPO / "claude-cube" / "host"))  # reuse claude-cube's tested PNG encoder
    from claudecube.snapshot import rgb565_to_png

    buf = ask(fd, b"snap", b"SNAP ")
    header = line_after(buf, b"SNAP ")
    width, height, mood = (int(v) for v in header.split())
    start = buf.find(b"SNAP ") + len(b"SNAP ") + len(header) + 1
    size = width * height * 2
    pixels = bytearray(buf[start:])
    if len(pixels) < size:
        pixels += read_until(fd, lambda b: len(b) >= size - (len(buf) - start))
    return rgb565_to_png(bytes(pixels[:size]), width, height), MOODS[mood]


def act(fd: int, mood: str, spot: tuple) -> str:
    """Hold the ball at `spot` until it feels `mood` or time runs out; returns what it felt."""
    x, y = spot
    deadline = time.monotonic() + MOOD_TIMEOUT_S
    while True:
        s = status(fd)
        if not s["held"] or abs(s["ball"][0] - x) > 1 or abs(s["ball"][1] - y) > 1:
            send(fd, f"place {x} {y} {HOLD_MS}")  # the first time, or a tap on the screen let go of it
        elif s["mood"] == mood or time.monotonic() > deadline:
            return s["mood"]
        time.sleep(POLL_S)


def shoot(fd: int, name: str, spot: tuple) -> tuple:
    """Screenshot the ball feeling `name`, checked against the mood the frame was drawn in: a late reply
    can catch it a moment later, say fuming instead of angry. Returns the PNG and that mood."""
    for _ in range(ATTEMPTS):
        act(fd, name, spot)
        time.sleep(SETTLE_S)
        png, felt = snap(fd)
        if felt == name:
            break
        send(fd, f"place 160 120 {HOLD_MS}")  # step back from the wall, so anger starts over
        time.sleep(POLL_S)
    return png, felt


def gallery(fd: int, out_dir: Path) -> int:
    """Act out every mood and screenshot it; returns how many came out as some other mood."""
    out_dir.mkdir(parents=True, exist_ok=True)
    mismatches = 0
    for name, spot in SHOTS:
        png, felt = shoot(fd, name, spot)
        path = out_dir / f"mood-{name}.png"
        path.write_bytes(png)
        mismatches += felt != name
        print(f"{path}  ({felt})" if felt == name else f"{path}  expected {name}, it felt {felt}")
    send(fd, "reset")
    return mismatches


def usage_error() -> int:
    print(__doc__, file=sys.stderr)
    return 2


def run(fd: int, argv: list) -> int:
    name, args = argv[0], argv[1:]
    if name == "status":
        print(json.dumps(status(fd), indent=2))
    elif name in ("level", "reset"):
        send(fd, name)
    elif name in ("tilt", "place"):
        try:
            numbers = [float(a) for a in args]
        except ValueError:
            return usage_error()
        if len(numbers) not in (2, 3):
            return usage_error()
        send(fd, " ".join([name] + [f"{v:g}" for v in numbers]))
    elif name == "snap":
        Path(args[0]).write_bytes(snap(fd)[0])
        print(args[0])
    elif name == "gallery":
        return 1 if gallery(fd, Path(args[0])) else 0
    return 0


def main(argv: list) -> int:
    commands = ("status", "level", "tilt", "place", "reset", "snap", "gallery")
    if not argv or argv[0] not in commands or (argv[0] in ("snap", "gallery") and len(argv) < 2):
        return usage_error()
    port = find_port()
    if port is None:
        print("No cube found on /dev/cu.usbmodem*", file=sys.stderr)
        return 2
    PAUSE_PATH.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
    PAUSE_PATH.touch()
    time.sleep(1.2)  # let a running bridge let go of the port
    fd = open_raw(port)
    try:
        return run(fd, argv)
    finally:
        os.close(fd)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
