"""Unix datagram socket helpers for the hook and bridge tests."""
from __future__ import annotations

import socket
from pathlib import Path

FILLER = b'{"ts":1,"event":"PreToolUse","sid":"s","pad":"' + b"x" * 300 + b'"}'


def fill_queue(path: Path) -> int:
    """Send until the receiver's queue is full; returns the errno the next send gets (ENOBUFS on macOS)."""
    with socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM) as sender:
        sender.setblocking(False)
        while True:
            try:
                sender.sendto(FILLER, str(path))
            except OSError as exc:
                return exc.errno


def stale_socket(path: Path) -> None:
    """Leave a socket file behind with nothing listening, as a crashed bridge does."""
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
    sock.bind(str(path))
    sock.close()


def listening_socket(path: Path) -> socket.socket:
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
    sock.bind(str(path))
    sock.setblocking(False)
    return sock
