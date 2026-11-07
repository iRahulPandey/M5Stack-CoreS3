"""Incremental reader for a Claude Code session transcript (one JSON object per line)."""
from __future__ import annotations

import json
import logging
import os
from dataclasses import dataclass, field
from datetime import datetime
from pathlib import Path
from typing import Dict, List, NamedTuple, Optional, Set

from claudecube.pricing import DEFAULT_CONTEXT_WINDOW, context_window, message_cost, token_count

log = logging.getLogger(__name__)

SYNTHETIC_MODEL = "<synthetic>"
SYSTEM_PROMPT_SOURCES = {"system"}
SUBAGENT_GLOB = "agent-*.jsonl"  # in <session id>/subagents/, next to the main <session id>.jsonl
LINE_ERRORS = (AttributeError, KeyError, OverflowError, RecursionError, TypeError, ValueError)


@dataclass(frozen=True)
class TranscriptStats:
    model: Optional[str] = None
    effort: Optional[str] = None
    speed: Optional[str] = None
    title: Optional[str] = None
    cwd: Optional[str] = None
    started_at: Optional[float] = None
    input_tokens: int = 0
    output_tokens: int = 0
    cache_read_tokens: int = 0
    cache_write_tokens: int = 0
    context_tokens: int = 0
    context_window: int = DEFAULT_CONTEXT_WINDOW
    cost_usd: Optional[float] = None
    turns: int = 0
    tool_calls: int = 0
    lines_added: int = 0
    lines_removed: int = 0


@dataclass(frozen=True)
class _Response:
    model: str
    speed: Optional[str]
    usage: dict


@dataclass
class _Accumulator:
    """Running state built up line by line. Private to the reader."""

    responses: Dict[str, _Response] = field(default_factory=dict)
    tool_ids: Set[str] = field(default_factory=set)
    model: Optional[str] = None
    effort: Optional[str] = None
    speed: Optional[str] = None
    title: Optional[str] = None
    cwd: Optional[str] = None
    started_at: Optional[float] = None
    context_tokens: int = 0
    turns: int = 0
    lines_added: int = 0
    lines_removed: int = 0


class TranscriptReader:
    """Reads only the bytes appended since the last poll, so polling a large transcript stays cheap.

    Subagents write their own transcripts; their usage counts towards the session totals, but the context,
    model, effort, title and turns are the main conversation's.
    """

    def __init__(self, path: Path) -> None:
        self._path = Path(path)
        self._main = _Source(self._path, sidechain=False)
        self._subagents: Dict[Path, _Source] = {}

    @property
    def path(self) -> Path:
        return self._path

    def poll(self) -> TranscriptStats:
        self._main.poll()
        for path in sorted(self._path.with_suffix("").joinpath("subagents").glob(SUBAGENT_GLOB)):
            self._subagents.setdefault(path, _Source(path, sidechain=True)).poll()
        return _snapshot(self._main.acc, [source.acc for source in self._subagents.values()])


class _Source:
    """One transcript file and everything read from it so far."""

    def __init__(self, path: Path, *, sidechain: bool) -> None:
        self._tail = _FileTail(path)
        self._sidechain = sidechain
        self.acc = _Accumulator()

    def poll(self) -> None:
        new = self._tail.read()
        if new.restarted:
            self.acc = _Accumulator()
        for line in new.lines:
            entry = _parse(line)
            if entry is None:
                continue
            try:
                _absorb(self.acc, entry, sidechain=self._sidechain)
            except LINE_ERRORS:  # a shape the guards below missed: skip the line, keep the rest of the file
                log.warning("Skipping a transcript line that could not be read", exc_info=True)


class _NewLines(NamedTuple):
    restarted: bool  # the file was truncated or replaced: forget everything read from it before
    lines: List[bytes]


class _FileTail:
    """The complete lines appended to a file since the last read."""

    def __init__(self, path: Path) -> None:
        self._path = path
        self._inode: Optional[int] = None
        self._offset = 0
        self._partial = b""

    def read(self) -> _NewLines:
        try:
            if self._unchanged(os.stat(self._path)):
                return _NewLines(False, [])
            with self._path.open("rb") as fh:
                info = os.fstat(fh.fileno())
                restarted = self._inode is not None and (info.st_ino != self._inode or info.st_size < self._offset)
                if restarted:
                    self._offset, self._partial = 0, b""
                self._inode = info.st_ino
                fh.seek(self._offset)
                chunk = fh.read()
        except OSError:
            return _NewLines(False, [])
        self._offset += len(chunk)
        *lines, self._partial = (self._partial + chunk).split(b"\n")
        return _NewLines(restarted, lines)

    def _unchanged(self, info: os.stat_result) -> bool:
        return info.st_ino == self._inode and info.st_size == self._offset


def _parse(line: bytes) -> Optional[dict]:
    if not line.strip():
        return None
    try:
        entry = json.loads(line)
    except (ValueError, RecursionError):
        log.debug("Skipping malformed transcript line", extra={"length": len(line)})
        return None
    return entry if isinstance(entry, dict) else None


def _absorb(acc: _Accumulator, entry: dict, *, sidechain: bool = False) -> None:
    """Fold one line into `acc`. Subagent (sidechain) lines add usage only, never session facts."""
    sidechain = sidechain or bool(entry.get("isSidechain"))
    kind = entry.get("type")
    if kind == "assistant":
        _absorb_assistant(acc, entry, sidechain)
    elif kind == "user":
        _absorb_user(acc, entry, sidechain)
    if sidechain:
        return
    if acc.started_at is None:
        acc.started_at = _timestamp(entry.get("timestamp"))
    if isinstance(entry.get("cwd"), str):
        acc.cwd = entry["cwd"]
    if kind == "custom-title" and isinstance(entry.get("customTitle"), str):
        acc.title = entry["customTitle"]


def _absorb_assistant(acc: _Accumulator, entry: dict, sidechain: bool) -> None:
    message = entry.get("message")
    if not isinstance(message, dict):
        return
    model = message.get("model")
    usage = message.get("usage")
    msg_id = message.get("id")
    if not isinstance(model, str) or model == SYNTHETIC_MODEL or not isinstance(usage, dict):
        return

    speed = usage.get("speed") if isinstance(usage.get("speed"), str) else None
    if isinstance(msg_id, str):
        acc.responses[msg_id] = _Response(model=model, speed=speed, usage=usage)

    for block in _list(message.get("content")):
        if isinstance(block, dict) and block.get("type") == "tool_use" and isinstance(block.get("id"), str):
            acc.tool_ids.add(block["id"])

    if sidechain:
        return
    acc.model, acc.speed = model, speed
    if isinstance(entry.get("effort"), str):
        acc.effort = entry["effort"]
    acc.context_tokens = sum(
        token_count(usage.get(k)) for k in ("input_tokens", "cache_read_input_tokens", "cache_creation_input_tokens")
    )


def _absorb_user(acc: _Accumulator, entry: dict, sidechain: bool) -> None:
    result = entry.get("toolUseResult")
    if isinstance(result, dict):
        added, removed = _changed_lines(result)
        acc.lines_added += added
        acc.lines_removed += removed
        return
    if not sidechain and _is_real_prompt(entry):
        acc.turns += 1


def _is_real_prompt(entry: dict) -> bool:
    if entry.get("isMeta") or entry.get("promptSource") in SYSTEM_PROMPT_SOURCES:
        return False
    message = entry.get("message")
    content = message.get("content") if isinstance(message, dict) else None
    if isinstance(content, str):
        return not content.lstrip().startswith("<")
    kinds = {b.get("type") for b in _list(content) if isinstance(b, dict) and isinstance(b.get("type"), str)}
    return "text" in kinds and "tool_result" not in kinds


def _changed_lines(result: dict) -> tuple[int, int]:
    added = removed = 0
    for hunk in _list(result.get("structuredPatch")):
        for line in _list(hunk.get("lines") if isinstance(hunk, dict) else None):
            if isinstance(line, str) and line.startswith("+"):
                added += 1
            elif isinstance(line, str) and line.startswith("-"):
                removed += 1
    if result.get("type") == "create" and isinstance(result.get("content"), str):
        added += len(result["content"].splitlines())
    return added, removed


def _snapshot(main: _Accumulator, subagents: List[_Accumulator]) -> TranscriptStats:
    everyone = [main, *subagents]
    responses = [r for acc in everyone for r in acc.responses.values()]
    costs = [message_cost(r.model, r.usage, r.speed) for r in responses]
    known = [c for c in costs if c is not None]
    return TranscriptStats(
        model=main.model,
        effort=main.effort,
        speed=main.speed,
        title=main.title,
        cwd=main.cwd,
        started_at=main.started_at,
        input_tokens=sum(token_count(r.usage.get("input_tokens")) for r in responses),
        output_tokens=sum(token_count(r.usage.get("output_tokens")) for r in responses),
        cache_read_tokens=sum(token_count(r.usage.get("cache_read_input_tokens")) for r in responses),
        cache_write_tokens=sum(token_count(r.usage.get("cache_creation_input_tokens")) for r in responses),
        context_tokens=main.context_tokens,
        context_window=context_window(main.model) if main.model else DEFAULT_CONTEXT_WINDOW,
        cost_usd=sum(known) if known else None,
        turns=main.turns,
        tool_calls=len(set().union(*(acc.tool_ids for acc in everyone))),
        lines_added=sum(acc.lines_added for acc in everyone),
        lines_removed=sum(acc.lines_removed for acc in everyone),
    )


def _timestamp(value: object) -> Optional[float]:
    if not isinstance(value, str):
        return None
    try:
        return datetime.fromisoformat(value.replace("Z", "+00:00")).timestamp()
    except (ValueError, OverflowError, OSError):
        return None


def _list(value: object) -> list:
    return value if isinstance(value, list) else []
