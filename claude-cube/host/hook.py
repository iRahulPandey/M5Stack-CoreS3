#!/usr/bin/env python3
"""Claude Code hook: forward a short summary of each event to the cube bridge.

Registered as an async command hook, so it never slows Claude down. It sends no
prompt text, no command lines and no file contents: only the event name, the tool
name and a short label such as a file name or the Bash description.
"""
from __future__ import annotations

import errno
import json
import os
import socket
import subprocess
import sys
import time
from enum import Enum
from pathlib import Path
from typing import Mapping

STATE_DIR = Path.home() / ".claude-cube"
SOCKET_PATH = STATE_DIR / "bridge.sock"
BRIDGE = Path(__file__).resolve().parent / "bridge.py"
MAX_DETAIL = 40
FILE_TOOLS = {"Read", "Edit", "Write", "NotebookEdit", "MultiEdit"}
DESCRIBED_TOOLS = {"Bash", "Agent", "Task"}
SEND_RETRIES = 5
RETRY_DELAY_S = 0.1
NO_BRIDGE_ERRNOS = {errno.ECONNREFUSED, errno.ENOENT}  # a stale socket file, or none at all
BUSY_ERRNOS = {errno.ENOBUFS, errno.EAGAIN}  # macOS reports a full datagram queue as ENOBUFS


class Delivery(Enum):
    SENT = "sent"
    NO_BRIDGE = "no bridge"  # start one
    BUSY = "busy"  # a bridge is running but has not drained its queue yet; try again shortly
    DROPPED = "dropped"  # for example too big for one datagram; trying again will not help


def summarize(payload: Mapping[str, object], now: float) -> dict:
    tool = _str(payload.get("tool_name"))
    tool_input = payload.get("tool_input")
    tool_input = tool_input if isinstance(tool_input, Mapping) else {}
    return {
        "ts": now,
        "event": _str(payload.get("hook_event_name")),
        "sid": _str(payload.get("session_id")),
        "transcript": _str(payload.get("transcript_path")),
        "cwd": _str(payload.get("cwd")),
        "tool": _short_tool(tool),
        "detail": _detail(tool, tool_input)[:MAX_DETAIL],
        "ntype": _str(payload.get("notification_type")),
        "tuid": _str(payload.get("tool_use_id")),
    }


def _short_tool(tool: str) -> str:
    """'mcp__Claude_Browser__computer' -> 'computer'."""
    return tool.rsplit("__", 1)[-1] if tool.startswith("mcp__") else tool


def _detail(tool: str, tool_input: Mapping[str, object]) -> str:
    if tool in FILE_TOOLS:
        path = _str(tool_input.get("file_path")) or _str(tool_input.get("notebook_path"))
        return Path(path).name if path else ""
    if tool in DESCRIBED_TOOLS:
        return " ".join(_str(tool_input.get("description")).split())
    if tool == "Skill":
        return _str(tool_input.get("skill"))
    return ""


def _str(value: object) -> str:
    return value if isinstance(value, str) else ""


def _send(message: bytes) -> Delivery:
    with socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM) as sock:
        try:
            sock.sendto(message, str(SOCKET_PATH))
            return Delivery.SENT
        except OSError as exc:
            if exc.errno in NO_BRIDGE_ERRNOS:
                return Delivery.NO_BRIDGE
            return Delivery.BUSY if exc.errno in BUSY_ERRNOS else Delivery.DROPPED


def _start_bridge() -> None:
    STATE_DIR.mkdir(mode=0o700, parents=True, exist_ok=True)
    with open(STATE_DIR / "bridge.log", "ab") as log:  # the child keeps its own copy of the descriptor
        subprocess.Popen(
            [sys.executable, str(BRIDGE)],
            stdin=subprocess.DEVNULL,
            stdout=log,
            stderr=log,
            start_new_session=True,
            close_fds=True,
        )


def main() -> int:
    try:
        payload = json.load(sys.stdin)
    except ValueError:
        return 0
    if not isinstance(payload, Mapping) or os.environ.get("CLAUDE_CUBE_DISABLE"):
        return 0
    message = json.dumps(summarize(payload, time.time()), separators=(",", ":")).encode("utf-8")
    delivery = _send(message)
    if delivery is Delivery.NO_BRIDGE:
        _start_bridge()
    for _ in range(SEND_RETRIES):
        if delivery in (Delivery.SENT, Delivery.DROPPED):
            break
        time.sleep(RETRY_DELAY_S)
        delivery = _send(message)
    return 0


if __name__ == "__main__":
    sys.exit(main())
