#!/usr/bin/env python3
"""Claude cube bridge: hook events + session transcript in, one JSON frame per update out to USB serial.

Started on demand by hook.py and exits after a few idle hours. Commands:
  bridge.py            run in the foreground (logs to stderr)
  bridge.py --print    print the frame for the newest session once, then exit
  bridge.py --stop     stop a running bridge (do this before flashing the cube)
"""
from __future__ import annotations

import json
import logging
import os
import select
import signal
import socket
import sys
import time
from pathlib import Path
from typing import Dict, Optional

sys.path.insert(0, str(Path(__file__).resolve().parent))

from claudecube.activity import HookEvent, SessionBoard  # noqa: E402
from claudecube.frame import build_frame, encode_frame, significant  # noqa: E402
from claudecube.serial_link import SerialLink  # noqa: E402
from claudecube.transcript import TranscriptReader, TranscriptStats  # noqa: E402

log = logging.getLogger("claude-cube")

STATE_DIR = Path.home() / ".claude-cube"
SOCKET_PATH = STATE_DIR / "bridge.sock"
PID_PATH = STATE_DIR / "bridge.pid"
PAUSE_PATH = STATE_DIR / "paused"  # firmware/flash.sh creates this so the bridge lets go of the port
LAST_FRAME_PATH = STATE_DIR / "last-frame.json"  # what the cube is showing, for debugging without the cube
PROJECTS_DIR = Path.home() / ".claude" / "projects"

TICK_S = 0.5
HEARTBEAT_S = 2.0
IDLE_EXIT_S = 3 * 3600
FALLBACK_MAX_AGE_S = 24 * 3600
FALLBACK_RESCAN_S = 10.0
MAX_READERS = 8
MAX_DATAGRAM = 8192


class TranscriptPicker:
    """Finds the transcript to show: the hook's session, or the newest one on disk before any hook fires."""

    def __init__(self) -> None:
        self._readers: Dict[str, TranscriptReader] = {}
        self._fallback: Optional[str] = None
        self._fallback_checked = 0.0

    def stats(self, path: str, now: float) -> TranscriptStats:
        path = path or self._newest(now) or ""
        if not path:
            return TranscriptStats()
        if path not in self._readers:
            if len(self._readers) >= MAX_READERS:
                self._readers.clear()
            self._readers[path] = TranscriptReader(Path(path))
        return self._readers[path].poll()

    def _newest(self, now: float) -> Optional[str]:
        if now - self._fallback_checked < FALLBACK_RESCAN_S:
            return self._fallback
        self._fallback_checked = now
        candidates = [p for p in PROJECTS_DIR.glob("*/*.jsonl") if p.is_file()]
        newest = max(candidates, key=lambda p: p.stat().st_mtime, default=None)
        fresh = newest is not None and now - newest.stat().st_mtime < FALLBACK_MAX_AGE_S
        self._fallback = str(newest) if fresh else None
        return self._fallback


def bind_socket() -> Optional[socket.socket]:
    """Bind the event socket, or return None when another bridge already owns it."""
    STATE_DIR.mkdir(mode=0o700, parents=True, exist_ok=True)
    if SOCKET_PATH.exists():
        with socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM) as probe:
            try:
                probe.sendto(b"{}", str(SOCKET_PATH))
                return None
            except OSError:
                SOCKET_PATH.unlink()
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
    sock.bind(str(SOCKET_PATH))
    os.chmod(SOCKET_PATH, 0o600)
    sock.setblocking(False)
    return sock


def drain_events(sock: socket.socket, board: SessionBoard) -> int:
    count = 0
    while True:
        try:
            data = sock.recv(MAX_DATAGRAM)
        except BlockingIOError:
            return count
        try:
            raw = json.loads(data)
        except ValueError:
            continue
        event = HookEvent.from_dict(raw) if isinstance(raw, dict) else None
        if event is not None:
            board.apply(event)
            count += 1


def write_last_frame(frame: dict) -> None:
    tmp = LAST_FRAME_PATH.with_suffix(".tmp")
    try:
        tmp.write_text(json.dumps(frame, indent=2), encoding="utf-8")
        os.replace(tmp, LAST_FRAME_PATH)
    except OSError as exc:
        log.warning("Cannot write %s: %s", LAST_FRAME_PATH, exc)


def run() -> int:
    sock = bind_socket()
    if sock is None:
        log.info("Another bridge is already running")
        return 0
    PID_PATH.write_text(str(os.getpid()), encoding="utf-8")
    stopping = []
    signal.signal(signal.SIGTERM, lambda *_: stopping.append(True))
    signal.signal(signal.SIGINT, lambda *_: stopping.append(True))

    board, picker, link = SessionBoard(), TranscriptPicker(), SerialLink()
    seq, last_sent, last_send_t, last_event_t = 0, None, 0.0, time.time()
    log.info("Bridge started (pid %d)", os.getpid())
    try:
        while not stopping:
            select.select([sock], [], [], TICK_S)
            now = time.time()
            if drain_events(sock, board):
                last_event_t = now
            if now - last_event_t > IDLE_EXIT_S:
                log.info("No Claude Code activity for %d hours, exiting", IDLE_EXIT_S // 3600)
                break
            if PAUSE_PATH.exists():
                link.close()
                last_sent = None
                continue
            activity = board.active()
            stats = picker.stats(activity.transcript_path if activity else "", now)
            frame = build_frame(activity, stats, now, seq + 1)
            reconnecting = not link.connected  # send straight away after a plug-in or reflash
            if reconnecting or significant(frame) != last_sent or now - last_send_t >= HEARTBEAT_S:
                write_last_frame(frame)
                if link.write(encode_frame(frame)):
                    seq, last_sent, last_send_t = seq + 1, significant(frame), now
    finally:
        link.close()
        sock.close()
        for path in (SOCKET_PATH, PID_PATH):
            try:
                path.unlink()
            except FileNotFoundError:
                pass
        log.info("Bridge stopped")
    return 0


def stop() -> int:
    try:
        pid = int(PID_PATH.read_text(encoding="utf-8").strip())
    except (OSError, ValueError):
        print("No bridge is running")
        return 0
    try:
        os.kill(pid, signal.SIGTERM)
        print(f"Stopped bridge (pid {pid})")
    except ProcessLookupError:
        PID_PATH.unlink()
        print("No bridge is running (removed a stale pid file)")
    return 0


def print_once() -> int:
    stats = TranscriptPicker().stats("", time.time())
    print(json.dumps(build_frame(None, stats, time.time(), 0), indent=2))
    return 0


def main(argv: list) -> int:
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
    if "--stop" in argv:
        return stop()
    if "--print" in argv:
        return print_once()
    return run()


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
