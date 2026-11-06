from __future__ import annotations

import errno
import io
import json
import sys
from pathlib import Path

import pytest

import hook
from hook import summarize
from sockets import fill_queue, listening_socket, stale_socket

BASE = {
    "session_id": "0d07bbb1-8d2b-40a4-81fa-be0a6f707c6f",
    "transcript_path": "/Users/rahulpandey/.claude/projects/x/0d07bbb1.jsonl",
    "cwd": "/Users/rahulpandey/Projects/issuetriage",
}


def payload(**fields: object) -> dict:
    return {**BASE, **fields}


class TestSummarize:
    def test_should_use_the_bash_description_and_never_the_command(self) -> None:
        out = summarize(payload(
            hook_event_name="PreToolUse",
            tool_name="Bash",
            tool_input={"command": "curl -H 'Authorization: Bearer FAKE-TOKEN-FOR-TESTS' https://api.example.com", "description": "Call the API"},
        ), now=10.0)
        assert out["detail"] == "Call the API"
        assert "FAKE-TOKEN" not in json.dumps(out)

    def test_should_show_only_the_file_name_for_file_tools(self) -> None:
        out = summarize(payload(
            hook_event_name="PreToolUse",
            tool_name="Edit",
            tool_input={"file_path": "/Users/rahulpandey/Projects/M5Stack-CoreS3/claude-cube/firmware/src/pages.cpp", "old_string": "a", "new_string": "b"},
        ), now=10.0)
        assert (out["tool"], out["detail"]) == ("Edit", "pages.cpp")

    def test_should_shorten_mcp_tool_names(self) -> None:
        out = summarize(payload(hook_event_name="PreToolUse", tool_name="mcp__Claude_Browser__computer", tool_input={}), now=1.0)
        assert out["tool"] == "computer"

    def test_should_truncate_long_details(self) -> None:
        out = summarize(payload(hook_event_name="PreToolUse", tool_name="Agent", tool_input={"description": "x" * 200}), now=1.0)
        assert len(out["detail"]) <= 40

    def test_should_carry_the_notification_type(self) -> None:
        out = summarize(payload(hook_event_name="Notification", notification_type="permission_prompt", message="Claude needs your permission"), now=1.0)
        assert out["ntype"] == "permission_prompt"
        assert "message" not in out

    def test_should_stamp_the_event_time_and_session(self) -> None:
        out = summarize(payload(hook_event_name="Stop"), now=42.5)
        assert (out["ts"], out["event"], out["sid"]) == (42.5, "Stop", BASE["session_id"])


@pytest.fixture
def bridge_socket(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> Path:
    """A relative socket path: absolute temp paths can pass the 104-byte limit on macOS."""
    monkeypatch.chdir(tmp_path)
    path = Path("bridge.sock")
    monkeypatch.setattr(hook, "SOCKET_PATH", path)
    monkeypatch.setattr(hook, "RETRY_DELAY_S", 0.0)
    return path


@pytest.fixture
def started(monkeypatch: pytest.MonkeyPatch) -> list:
    calls: list = []
    monkeypatch.setattr(hook, "_start_bridge", lambda: calls.append(True))
    return calls


def run_hook(monkeypatch: pytest.MonkeyPatch, event: dict) -> int:
    monkeypatch.setattr(sys, "stdin", io.StringIO(json.dumps(event)))
    return hook.main()


class TestDelivery:
    def test_should_start_a_bridge_when_no_socket_exists(self, bridge_socket: Path, started: list, monkeypatch: pytest.MonkeyPatch) -> None:
        assert run_hook(monkeypatch, payload(hook_event_name="Stop")) == 0
        assert started == [True]

    def test_should_start_a_bridge_when_the_socket_is_stale(self, bridge_socket: Path, started: list, monkeypatch: pytest.MonkeyPatch) -> None:
        stale_socket(bridge_socket)
        run_hook(monkeypatch, payload(hook_event_name="Stop"))
        assert started == [True]

    def test_should_deliver_to_a_running_bridge(self, bridge_socket: Path, started: list, monkeypatch: pytest.MonkeyPatch) -> None:
        with listening_socket(bridge_socket) as server:
            run_hook(monkeypatch, payload(hook_event_name="Stop"))
            assert json.loads(server.recv(8192))["event"] == "Stop"
        assert started == []

    @pytest.mark.skipif(sys.platform != "darwin", reason="macOS reports a full datagram queue as ENOBUFS")
    def test_should_not_start_a_second_bridge_when_the_queue_is_full(self, bridge_socket: Path, started: list, monkeypatch: pytest.MonkeyPatch) -> None:
        with listening_socket(bridge_socket):
            assert fill_queue(bridge_socket) == errno.ENOBUFS
            run_hook(monkeypatch, payload(hook_event_name="Stop"))
        assert started == []

    def test_should_not_start_a_bridge_for_a_message_too_big_to_send(self, bridge_socket: Path, started: list, monkeypatch: pytest.MonkeyPatch) -> None:
        with listening_socket(bridge_socket):
            run_hook(monkeypatch, payload(hook_event_name="Stop", cwd="/Users/rahulpandey/" + "deep/" * 2_000))
        assert started == []

    def test_should_ignore_input_that_is_not_json(self, bridge_socket: Path, started: list, monkeypatch: pytest.MonkeyPatch) -> None:
        monkeypatch.setattr(sys, "stdin", io.StringIO("not json"))
        assert hook.main() == 0
        assert started == []

    def test_should_do_nothing_when_disabled(self, bridge_socket: Path, started: list, monkeypatch: pytest.MonkeyPatch) -> None:
        monkeypatch.setenv("CLAUDE_CUBE_DISABLE", "1")
        run_hook(monkeypatch, payload(hook_event_name="Stop"))
        assert started == []


class TestStartBridge:
    def test_should_launch_the_bridge_detached_with_a_log_file(self, tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
        launched: list = []
        monkeypatch.setattr(hook, "STATE_DIR", tmp_path / "state")
        monkeypatch.setattr(hook.subprocess, "Popen", lambda args, **kwargs: launched.append((args, kwargs)))
        hook._start_bridge()
        (args, kwargs), = launched
        assert args == [sys.executable, str(hook.BRIDGE)]
        assert kwargs["start_new_session"] is True
        assert (tmp_path / "state" / "bridge.log").exists()
