#!/usr/bin/env python3
"""Claude cube bridge: hook events + session transcript in, one JSON frame per update out to USB serial.

Started on demand by hook.py and exits after a few idle hours. Commands:
  bridge.py            run in the foreground (logs to stderr)
  bridge.py --print    print the frame for the newest session once, then exit
  bridge.py --status   say whether a bridge is running, whether it is paused, and which port it would use
  bridge.py --stop     stop a running bridge
"""
from __future__ import annotations

import errno
import fcntl
import json
import logging
import os
import select
import signal
import socket
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Optional

sys.path.insert(0, str(Path(__file__).resolve().parent))

from claudecube.activity import HookEvent, SessionActivity, SessionBoard  # noqa: E402
from claudecube.frame import build_frame, encode_frame, significant  # noqa: E402
from claudecube.serial_link import SerialLink, find_port  # noqa: E402
from claudecube.transcript import TranscriptReader, TranscriptStats  # noqa: E402

log = logging.getLogger("claude-cube")

STATE_DIR = Path.home() / ".claude-cube"
SOCKET_PATH = STATE_DIR / "bridge.sock"
PID_PATH = STATE_DIR / "bridge.pid"
PAUSE_PATH = STATE_DIR / "paused"  # flash.sh and snap.py create this so the bridge lets go of the port
LAST_FRAME_PATH = STATE_DIR / "last-frame.json"  # what the cube is showing, for debugging without the cube
PROJECTS_DIR = Path.home() / ".claude" / "projects"

TICK_S = 0.5
HEARTBEAT_S = 2.0
IDLE_EXIT_S = 3 * 3600
FALLBACK_MAX_AGE_S = 24 * 3600
FALLBACK_RESCAN_S = 10.0
MAX_READERS = 8
MAX_DATAGRAM = 8192
NO_BRIDGE_ERRNOS = {errno.ECONNREFUSED, errno.ENOENT}  # same rule as hook.py: anything else means a bridge is there


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


@dataclass(frozen=True)
class Claim:
    """This process is the running bridge: it holds the pid file lock and the event socket."""

    sock: socket.socket
    lock_fd: int
    socket_inode: int

    def release(self) -> None:
        """Close up, removing the socket and pid file only if a newer bridge has not replaced them."""
        self.sock.close()
        if _inode(SOCKET_PATH) == self.socket_inode:
            SOCKET_PATH.unlink(missing_ok=True)
        if _inode(PID_PATH) == os.fstat(self.lock_fd).st_ino:
            PID_PATH.unlink(missing_ok=True)
        os.close(self.lock_fd)


def claim() -> Optional[Claim]:
    """Become the one running bridge, or return None when another bridge already is."""
    STATE_DIR.mkdir(mode=0o700, parents=True, exist_ok=True)
    lock_fd = _lock_pid_file()
    if lock_fd is None:
        return None
    try:
        if _socket_is_live():  # a bridge from before the pid file lock, which holds no lock
            os.close(lock_fd)
            return None
        SOCKET_PATH.unlink(missing_ok=True)
        sock = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
        sock.bind(str(SOCKET_PATH))
    except OSError:
        os.close(lock_fd)
        raise
    os.chmod(SOCKET_PATH, 0o600)
    sock.setblocking(False)
    os.ftruncate(lock_fd, 0)
    os.write(lock_fd, str(os.getpid()).encode("ascii"))
    return Claim(sock=sock, lock_fd=lock_fd, socket_inode=os.stat(SOCKET_PATH).st_ino)


def _lock_pid_file() -> Optional[int]:
    fd = os.open(PID_PATH, os.O_RDWR | os.O_CREAT, 0o600)
    try:
        fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except BlockingIOError:
        os.close(fd)
        return None
    if _inode(PID_PATH) != os.fstat(fd).st_ino:  # an exiting bridge removed the file after we opened it
        os.close(fd)
        return None
    return fd


def _socket_is_live() -> bool:
    with socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM) as probe:
        probe.setblocking(False)
        try:
            probe.sendto(b"{}", str(SOCKET_PATH))
        except OSError as exc:
            return exc.errno not in NO_BRIDGE_ERRNOS  # a full queue (ENOBUFS) still means someone is reading
    return True


def _inode(path: Path) -> Optional[int]:
    try:
        return os.stat(path).st_ino
    except FileNotFoundError:
        return None


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


class FrameSender:
    """Sends a frame when it changed, on the heartbeat, and straight after the cube (re)connects."""

    def __init__(self, link: SerialLink) -> None:
        self._link = link
        self._seq = 0
        self._last_sent: Optional[dict] = None
        self._last_saved: Optional[dict] = None
        self._last_send_t = 0.0

    def reset(self) -> None:
        """Forget what the cube is showing, so the next frame goes out at once."""
        self._last_sent = None

    def offer(self, activity: Optional[SessionActivity], stats: TranscriptStats, now: float) -> None:
        frame = build_frame(activity, stats, now, self._seq + 1)
        key = significant(frame)
        reconnecting = not self._link.connected  # send straight away after a plug-in or reflash
        if not (reconnecting or key != self._last_sent or now - self._last_send_t >= HEARTBEAT_S):
            return
        sent = self._link.write(encode_frame(frame))
        if sent or key != self._last_saved:  # unplugged: rewrite the file only when the frame changes
            write_last_frame(frame)
            self._last_saved = key
        if sent:
            self._seq, self._last_sent, self._last_send_t = self._seq + 1, key, now


class Bridge:
    """What the bridge does on each tick: read hook events, pick the session, send its frame."""

    def __init__(self, sock: socket.socket, link: SerialLink) -> None:
        self._sock = sock
        self._link = link
        self._board = SessionBoard()
        self._picker = TranscriptPicker()
        self._sender = FrameSender(link)
        self._paused_since: Optional[float] = None
        self.last_event_t = time.time()

    def tick(self, now: float) -> None:
        if drain_events(self._sock, self._board):
            self.last_event_t = now
        if self._update_pause(now):
            return
        activity = self._board.active()
        stats = self._picker.stats(activity.transcript_path if activity else "", now)
        self._sender.offer(activity, stats, now)

    def _update_pause(self, now: float) -> bool:
        """True while paused. Logs each change: a leftover pause file would otherwise blank the cube silently."""
        paused = PAUSE_PATH.exists()
        if paused and self._paused_since is None:
            log.info("Paused while %s exists: the serial port is free for other tools", PAUSE_PATH)
            self._link.close()
            self._sender.reset()
            self._paused_since = now
        elif not paused and self._paused_since is not None:
            log.info("Resumed after %d min paused", max(now - self._paused_since, 0) // 60)
            self._paused_since = None
        return paused


def run() -> int:
    owner = claim()
    if owner is None:
        log.info("Another bridge is already running")
        return 0
    stopping = []
    signal.signal(signal.SIGTERM, lambda *_: stopping.append(True))
    signal.signal(signal.SIGINT, lambda *_: stopping.append(True))

    link = SerialLink()
    loop = Bridge(owner.sock, link)
    log.info("Bridge started (pid %d)", os.getpid())
    try:
        while not stopping:
            select.select([owner.sock], [], [], TICK_S)
            now = time.time()
            loop.tick(now)
            if now - loop.last_event_t > IDLE_EXIT_S:
                log.info("No Claude Code activity for %d hours, exiting", IDLE_EXIT_S // 3600)
                break
    finally:
        link.close()
        owner.release()
        log.info("Bridge stopped")
    return 0


def stop() -> int:
    pid = _read_pid()
    if pid is None or not _is_bridge(pid):
        print("No bridge is running")
        return 0
    try:
        os.kill(pid, signal.SIGTERM)
    except ProcessLookupError:
        print("No bridge is running")
        return 0
    except PermissionError:
        print(f"Not allowed to stop the bridge (pid {pid})", file=sys.stderr)
        return 1
    print(f"Stopped bridge (pid {pid})")
    return 0


def _read_pid() -> Optional[int]:
    try:
        pid = int(PID_PATH.read_text(encoding="utf-8").strip())
    except (OSError, ValueError):
        return None
    return pid if pid > 0 else None  # kill(0) or kill(-1) would signal whole groups of processes


def _is_bridge(pid: int) -> bool:
    """True when `pid` runs bridge.py, so a stale pid file never gets an unrelated process killed."""
    result = subprocess.run(["ps", "-p", str(pid), "-o", "command="], capture_output=True, text=True, check=False)
    return result.returncode == 0 and any(os.path.basename(arg) == "bridge.py" for arg in result.stdout.split())


def status() -> int:
    pid = _read_pid()
    running = pid is not None and _is_bridge(pid)
    print(f"bridge: running (pid {pid})" if running else "bridge: not running")
    print(_pause_status(time.time()))
    port = find_port()
    print(f"port: {port}" if port else "port: no cube found")
    return 0


def _pause_status(now: float) -> str:
    try:
        since = PAUSE_PATH.stat().st_mtime
    except FileNotFoundError:
        return "paused: no"
    minutes = int(max(now - since, 0)) // 60
    return f"paused: yes, for {minutes} min ({PAUSE_PATH} exists; delete it to resume)"


def print_once() -> int:
    stats = TranscriptPicker().stats("", time.time())
    print(json.dumps(build_frame(None, stats, time.time(), 0), indent=2))
    return 0


def main(argv: list) -> int:
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
    if "--stop" in argv:
        return stop()
    if "--status" in argv:
        return status()
    if "--print" in argv:
        return print_once()
    return run()


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
