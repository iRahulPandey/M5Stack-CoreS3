from __future__ import annotations

import errno
import os
import signal
import subprocess
import sys
import time
from pathlib import Path
from typing import Callable, Iterator

import pytest

import bridge
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
    return state


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
