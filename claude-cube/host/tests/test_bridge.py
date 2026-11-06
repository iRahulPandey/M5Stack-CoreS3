from __future__ import annotations

import errno
import json
import logging
import os
import signal
import socket
import subprocess
import sys
import time
from pathlib import Path
from typing import Callable, Iterator

import pytest

import bridge
from claudecube.activity import SessionBoard
from claudecube.transcript import TranscriptStats
from sockets import fill_queue, listening_socket, stale_socket


@pytest.fixture
def state_dir(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> Path:
    """Point the bridge's state at a temp dir, by relative paths: socket paths are limited to 104 bytes on macOS."""
    monkeypatch.chdir(tmp_path)
    state = Path("state")
    monkeypatch.setattr(bridge, "STATE_DIR", state)
    monkeypatch.setattr(bridge, "SOCKET_PATH", state / "bridge.sock")
    monkeypatch.setattr(bridge, "PID_PATH", state / "bridge.pid")
    monkeypatch.setattr(bridge, "PAUSE_PATH", state / "paused")
    monkeypatch.setattr(bridge, "LAST_FRAME_PATH", state / "last-frame.json")
    monkeypatch.setattr(bridge, "PROJECTS_DIR", Path("projects"))
    return state


class FakeLink:
    """Stands in for SerialLink: records frames instead of touching a serial port."""

    def __init__(self, *, accept: bool = True) -> None:
        self.accept = accept
        self.frames: list = []
        self.connected = False
        self.closed = 0

    def write(self, data: bytes) -> bool:
        if self.accept:
            self.frames.append(json.loads(data))
            self.connected = True
        return self.accept

    def close(self) -> None:
        self.closed += 1
        self.connected = False


@pytest.fixture
def running(state_dir: Path) -> Iterator["bridge.Claim"]:
    owner = bridge.claim()
    assert owner is not None
    yield owner
    owner.release()


def send_event(name: str, ts: float, **fields: object) -> None:
    message = {"ts": ts, "event": name, "sid": "0d07bbb1", "transcript": "", "cwd": "/Users/rahulpandey/Projects/issuetriage", **fields}
    with socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM) as sock:
        sock.sendto(json.dumps(message).encode("utf-8"), str(bridge.SOCKET_PATH))


class TestClaim:
    def test_should_bind_the_socket_and_record_the_pid(self, state_dir: Path) -> None:
        claim = bridge.claim()
        assert claim is not None
        try:
            assert bridge.SOCKET_PATH.exists()
            assert bridge.PID_PATH.read_text(encoding="utf-8") == str(os.getpid())
        finally:
            claim.release()

    def test_should_refuse_a_second_bridge_while_the_first_holds_the_lock(self, state_dir: Path) -> None:
        first = bridge.claim()
        assert first is not None
        try:
            assert bridge.claim() is None
        finally:
            first.release()

    def test_should_take_over_a_stale_socket(self, state_dir: Path) -> None:
        state_dir.mkdir()
        stale_socket(bridge.SOCKET_PATH)
        claim = bridge.claim()
        assert claim is not None
        claim.release()

    @pytest.mark.skipif(sys.platform != "darwin", reason="macOS reports a full datagram queue as ENOBUFS")
    def test_should_leave_a_live_socket_alone_when_its_queue_is_full(self, state_dir: Path) -> None:
        state_dir.mkdir()
        with listening_socket(bridge.SOCKET_PATH):  # a bridge from before the pid lock existed
            inode = os.stat(bridge.SOCKET_PATH).st_ino
            assert fill_queue(bridge.SOCKET_PATH) == errno.ENOBUFS
            assert bridge.claim() is None
            assert os.stat(bridge.SOCKET_PATH).st_ino == inode

    def test_should_remove_its_socket_and_pid_file_on_release(self, state_dir: Path) -> None:
        claim = bridge.claim()
        assert claim is not None
        claim.release()
        assert not bridge.SOCKET_PATH.exists()
        assert not bridge.PID_PATH.exists()

    def test_should_leave_a_newer_bridges_files_alone_on_release(self, state_dir: Path) -> None:
        claim = bridge.claim()
        assert claim is not None
        bridge.SOCKET_PATH.unlink()
        bridge.PID_PATH.unlink()
        with listening_socket(bridge.SOCKET_PATH):
            bridge.PID_PATH.write_text("99999", encoding="utf-8")
            claim.release()
            assert bridge.SOCKET_PATH.exists()
            assert bridge.PID_PATH.read_text(encoding="utf-8") == "99999"


@pytest.fixture
def child() -> Iterator[Callable[..., subprocess.Popen]]:
    """Start throwaway processes; any still running are killed after the test."""
    procs: list = []

    def start(*args: str) -> subprocess.Popen:
        proc = subprocess.Popen(list(args))
        procs.append(proc)
        return proc

    yield start
    for proc in procs:
        proc.kill()
        proc.wait()


def write_pid(pid: int) -> None:
    bridge.STATE_DIR.mkdir(exist_ok=True)
    bridge.PID_PATH.write_text(str(pid), encoding="utf-8")


class TestStop:
    def test_should_stop_a_running_bridge(self, state_dir: Path, child: Callable[..., subprocess.Popen], capsys: pytest.CaptureFixture) -> None:
        proc = child(sys.executable, "-c", "import time; time.sleep(30)", "/x/claude-cube/host/bridge.py")
        write_pid(proc.pid)
        time.sleep(0.2)  # let the child exec so ps shows its command line
        assert bridge.stop() == 0
        assert proc.wait(timeout=5) == -signal.SIGTERM
        assert f"Stopped bridge (pid {proc.pid})" in capsys.readouterr().out

    def test_should_not_signal_a_process_that_is_not_a_bridge(self, state_dir: Path, child: Callable[..., subprocess.Popen], capsys: pytest.CaptureFixture) -> None:
        proc = child("sleep", "30")
        write_pid(proc.pid)
        assert bridge.stop() == 0
        assert proc.poll() is None
        assert "No bridge is running" in capsys.readouterr().out

    def test_should_report_no_bridge_for_a_process_that_has_exited(self, state_dir: Path, child: Callable[..., subprocess.Popen], capsys: pytest.CaptureFixture) -> None:
        proc = child("true")
        proc.wait()
        write_pid(proc.pid)
        assert bridge.stop() == 0
        assert "No bridge is running" in capsys.readouterr().out

    @pytest.mark.parametrize("content", ["", "not a pid", "0", "-1"])
    def test_should_never_signal_for_an_unusable_pid_file(self, state_dir: Path, content: str, monkeypatch: pytest.MonkeyPatch) -> None:
        bridge.STATE_DIR.mkdir()
        bridge.PID_PATH.write_text(content, encoding="utf-8")
        monkeypatch.setattr(bridge.os, "kill", lambda *args: pytest.fail("must not signal"))
        assert bridge.stop() == 0

    def test_should_report_when_not_allowed_to_signal_the_bridge(self, state_dir: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture) -> None:
        write_pid(4242)
        monkeypatch.setattr(bridge, "_is_bridge", lambda pid: True)

        def refuse(pid: int, sig: int) -> None:
            raise PermissionError(errno.EPERM, "Operation not permitted")

        monkeypatch.setattr(bridge.os, "kill", refuse)
        assert bridge.stop() == 1
        assert "pid 4242" in capsys.readouterr().err


class TestPause:
    def test_should_release_the_port_and_log_while_paused(self, running: "bridge.Claim", caplog: pytest.LogCaptureFixture) -> None:
        link = FakeLink()
        loop = bridge.Bridge(running.sock, link)
        loop.tick(1_000.0)
        assert len(link.frames) == 1
        bridge.PAUSE_PATH.touch()
        with caplog.at_level(logging.INFO, logger="claude-cube"):
            loop.tick(1_001.0)
            loop.tick(1_010.0)
        assert link.closed == 1
        assert len(link.frames) == 1
        assert [r.getMessage() for r in caplog.records if "aused" in r.getMessage()] == [
            f"Paused while {bridge.PAUSE_PATH} exists: the serial port is free for other tools"
        ]

    def test_should_resend_and_log_when_the_pause_ends(self, running: "bridge.Claim", caplog: pytest.LogCaptureFixture) -> None:
        link = FakeLink()
        loop = bridge.Bridge(running.sock, link)
        bridge.PAUSE_PATH.touch()
        loop.tick(1_000.0)
        bridge.PAUSE_PATH.unlink()
        with caplog.at_level(logging.INFO, logger="claude-cube"):
            loop.tick(1_001.0)
        assert len(link.frames) == 1
        assert "Resumed" in caplog.text


class TestStatus:
    def test_should_report_a_running_bridge_the_pause_and_the_port(
        self, state_dir: Path, child: Callable[..., subprocess.Popen], monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture
    ) -> None:
        proc = child(sys.executable, "-c", "import time; time.sleep(30)", "/x/claude-cube/host/bridge.py")
        write_pid(proc.pid)
        bridge.PAUSE_PATH.touch()
        monkeypatch.setattr(bridge, "find_port", lambda: "/dev/cu.usbmodem101")
        time.sleep(0.2)  # let the child exec so ps shows its command line
        assert bridge.main(["--status"]) == 0
        out = capsys.readouterr().out.splitlines()
        assert out[0] == f"bridge: running (pid {proc.pid})"
        assert out[1].startswith("paused: yes, for 0 min")
        assert str(bridge.PAUSE_PATH) in out[1]
        assert out[2] == "port: /dev/cu.usbmodem101"

    def test_should_report_a_stopped_bridge_without_a_cube(self, state_dir: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture) -> None:
        monkeypatch.setattr(bridge, "find_port", lambda: None)
        assert bridge.main(["--status"]) == 0
        assert capsys.readouterr().out.splitlines() == ["bridge: not running", "paused: no", "port: no cube found"]


class TestFrameSender:
    @pytest.fixture
    def saved(self, state_dir: Path, monkeypatch: pytest.MonkeyPatch) -> list:
        frames: list = []
        monkeypatch.setattr(bridge, "write_last_frame", frames.append)
        return frames

    def test_should_not_rewrite_the_last_frame_while_the_cube_is_unplugged(self, saved: list) -> None:
        sender = bridge.FrameSender(FakeLink(accept=False))
        for tick in range(10):
            sender.offer(None, TranscriptStats(), 1_000.0 + tick * bridge.TICK_S)
        assert len(saved) == 1

    def test_should_save_a_changed_frame_while_the_cube_is_unplugged(self, saved: list) -> None:
        sender = bridge.FrameSender(FakeLink(accept=False))
        sender.offer(None, TranscriptStats(), 1_000.0)
        sender.offer(None, TranscriptStats(output_tokens=120), 1_000.5)
        assert [f["tout"] for f in saved] == [0, 120]

    def test_should_send_on_change_and_on_the_heartbeat(self, saved: list) -> None:
        link = FakeLink()
        sender = bridge.FrameSender(link)
        sender.offer(None, TranscriptStats(), 1_000.0)
        sender.offer(None, TranscriptStats(), 1_000.5)
        sender.offer(None, TranscriptStats(turns=1), 1_001.0)
        sender.offer(None, TranscriptStats(turns=1), 1_001.0 + bridge.HEARTBEAT_S)
        assert [f["seq"] for f in link.frames] == [1, 2, 3]
        assert len(saved) == 3


class VanishingPath(type(Path())):  # type: ignore[misc]
    """A glob result whose file is deleted before it can be stat()ed."""

    def is_file(self) -> bool:
        return True


class TestMalformedInput:
    def test_should_skip_datagrams_nested_too_deep_to_parse(self, running: "bridge.Claim") -> None:
        with socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM) as sock:
            sock.sendto(b"[" * 1_500, str(bridge.SOCKET_PATH))
        send_event("Stop", 1_000.0)
        board = SessionBoard()
        assert bridge.drain_events(running.sock, board) == 1
        assert board.active() is not None

    def test_should_skip_a_transcript_that_vanishes_during_the_scan(self, state_dir: Path, monkeypatch: pytest.MonkeyPatch) -> None:
        project = Path("projects") / "-Users-rahulpandey-Projects-issuetriage"
        project.mkdir(parents=True)
        kept = project / "0d07bbb1.jsonl"
        kept.write_text("", encoding="utf-8")

        class Projects:
            def glob(self, pattern: str) -> list:
                return [VanishingPath(project / "gone.jsonl"), kept]

        monkeypatch.setattr(bridge, "PROJECTS_DIR", Projects())
        assert bridge.TranscriptPicker()._newest(time.time()) == str(kept)

    def test_should_log_a_failing_tick_once_a_minute_and_carry_on(self, running: "bridge.Claim", caplog: pytest.LogCaptureFixture) -> None:
        class Broken:
            def stats(self, path: str, now: float) -> TranscriptStats:
                raise RuntimeError("unexpected transcript shape")

        loop = bridge.Bridge(running.sock, FakeLink())
        loop._picker = Broken()  # type: ignore[assignment]
        with caplog.at_level(logging.ERROR, logger="claude-cube"):
            for now in (1_000.0, 1_000.5, 1_000.0 + bridge.ERROR_LOG_INTERVAL_S):
                loop.tick(now)
        assert [r.getMessage() for r in caplog.records].count("Tick failed; carrying on") == 2
