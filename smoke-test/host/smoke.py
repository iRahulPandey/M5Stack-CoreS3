#!/usr/bin/env python3
"""Read or re-run the CoreS3 smoke test over USB serial.

  smoke.py               print the report from the last run (the firmware runs once at boot)
  smoke.py --rerun       run every check again (about 15 s, one beep), then print the report
  smoke.py --save FILE   also write the report JSON to FILE
  smoke.py --snap DIR    save a PNG of each of the three screens to DIR

Keeps the claude-cube bridge paused (~/.claude-cube/paused) so it does not write into this firmware;
claude-cube/firmware/flash.sh lifts the pause when the cube firmware goes back on. Exits 1 if any check failed.
"""
from __future__ import annotations

import argparse
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

from smokereport import exit_code, find_report, format_report

REPO = Path(__file__).resolve().parents[2]
PAUSE_PATH = Path.home() / ".claude-cube" / "paused"
PORT_ENV = "CLAUDE_CUBE_PORT"
REPORT_TIMEOUT_S = 5.0
RERUN_TIMEOUT_S = 40.0
SNAP_TIMEOUT_S = 5.0
SCREEN_NAMES = ["checks-1", "checks-2", "memory"]  # order of Screen in firmware/src/screen.h


def find_port() -> Optional[str]:
    explicit = os.environ.get(PORT_ENV)
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


def read_until(fd: int, done: Callable[[bytearray], bool], timeout_s: float) -> bytearray:
    buf = bytearray()
    deadline = time.monotonic() + timeout_s
    while not done(buf):
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError("no answer from the cube; is the smoke-test firmware flashed?")
        if select.select([fd], [], [], remaining)[0]:
            buf += os.read(fd, 65536)
    return buf


def request_report(fd: int, rerun: bool) -> dict:
    os.write(fd, b"\nrerun\n" if rerun else b"\nreport\n")
    buf = read_until(fd, lambda b: find_report(bytes(b)) is not None, RERUN_TIMEOUT_S if rerun else REPORT_TIMEOUT_S)
    report = find_report(bytes(buf))
    assert report is not None
    return report


def snap_screens(fd: int, out_dir: Path) -> None:
    sys.path.insert(0, str(REPO / "claude-cube" / "host"))  # reuse claude-cube's tested PNG encoder
    from claudecube.snapshot import rgb565_to_png

    out_dir.mkdir(parents=True, exist_ok=True)
    command = b"\nsnap\n"
    for _ in SCREEN_NAMES:
        os.write(fd, command)
        buf = read_until(fd, lambda b: b"SNAP " in b and b"\n" in b[b.find(b"SNAP "):], SNAP_TIMEOUT_S)
        start = buf.find(b"SNAP ")
        end = buf.find(b"\n", start)
        width, height, screen = (int(v) for v in buf[start + 5:end].split())
        size = width * height * 2
        pixels = buf[end + 1:] + read_until(fd, lambda b: len(b) >= size - len(buf[end + 1:]), SNAP_TIMEOUT_S)
        path = out_dir / f"smoke-{SCREEN_NAMES[screen]}.png"
        path.write_bytes(rgb565_to_png(bytes(pixels[:size]), width, height))
        print(path)
        command = b"next\nsnap\n"


def main(argv: list) -> int:
    parser = argparse.ArgumentParser(description="Read or re-run the CoreS3 smoke test.")
    parser.add_argument("--rerun", action="store_true", help="run every check again first")
    parser.add_argument("--save", type=Path, help="write the report JSON to this file")
    parser.add_argument("--snap", type=Path, help="save PNGs of the three screens to this folder")
    args = parser.parse_args(argv)

    port = find_port()
    if port is None:
        print("No cube found on /dev/cu.usbmodem*", file=sys.stderr)
        return 2
    PAUSE_PATH.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
    PAUSE_PATH.touch()
    time.sleep(1.2)  # let a running bridge let go of the port
    fd = open_raw(port)
    try:
        report = request_report(fd, args.rerun)
        if args.snap:
            snap_screens(fd, args.snap)
    finally:
        os.close(fd)
    print(format_report(report), end="")
    if args.save:
        args.save.parent.mkdir(parents=True, exist_ok=True)
        args.save.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return exit_code(report)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
