#!/usr/bin/env python3
"""Talk to the Sandbox over USB serial. Positions are in cells: 160 x 106, 0 0 at the top left of the box.

  sandbox.py status                       gravity, shake, grain counts, frame rate (JSON)
  sandbox.py snap FILE                    screenshot the screen to FILE (PNG)
  sandbox.py clear | scene                empty the box, or put the starting scene back
  sandbox.py tool NAME                    sand, water, wall or erase
  sandbox.py paint MATERIAL X Y R [D]     a disc of sand, water, wall or empty (D: percent sprinkled)
  sandbox.py line MATERIAL X0 Y0 X1 Y1 R
  sandbox.py gravity DX DY [MS]           pretend the board is held so down is DX DY (-1..1), default 5 s
  sandbox.py shake [MS]                   pretend it is being shaken, default 5 s
  sandbox.py demo DIR                     act out the scene and screenshot it to DIR/sandbox-<step>.png

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
COMMANDS = {  # name: how many arguments it takes, at least and at most
    "status": (0, 0), "snap": (1, 1), "clear": (0, 0), "scene": (0, 0), "tool": (1, 1),
    "paint": (4, 5), "line": (6, 6), "gravity": (2, 3), "shake": (0, 1), "demo": (1, 1),
}

# (screenshot name, commands to send first, seconds to wait before the screenshot)
DEMO = [
    ("start", ["scene", "gravity 0 0 60000"], 0.5),
    ("falling", ["gravity 0 1 60000"], 0.9),
    ("settled", [], 4.0),
    ("tipped", ["gravity 1 0 60000"], 3.0),
    ("shaken", ["gravity 0 1 60000", "shake 1500"], 0.8),
    ("resettled", [], 4.0),
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
            raise TimeoutError("no answer from the Sandbox; is its firmware flashed?")
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


def demo(fd: int, out_dir: Path) -> None:
    out_dir.mkdir(parents=True, exist_ok=True)
    for name, commands, wait_s in DEMO:
        for command in commands:
            send(fd, command)
        time.sleep(wait_s)
        path = out_dir / f"sandbox-{name}.png"
        path.write_bytes(snap(fd))
        print(path)
    send(fd, "gravity 0 1 0")  # hand gravity back to the board


def run(fd: int, name: str, args: list) -> int:
    if name == "status":
        print(json.dumps(status(fd), indent=2))
    elif name == "snap":
        Path(args[0]).write_bytes(snap(fd))
        print(args[0])
    elif name == "demo":
        demo(fd, Path(args[0]))
    else:
        send(fd, " ".join([name] + args))
    return 0


def main(argv: list) -> int:
    if not argv or argv[0] not in COMMANDS or not COMMANDS[argv[0]][0] <= len(argv) - 1 <= COMMANDS[argv[0]][1]:
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
