"""Incremental reader for a Claude Code session transcript (one JSON object per line)."""
from __future__ import annotations

import json
import logging
from dataclasses import dataclass, field
from datetime import datetime
from pathlib import Path
from typing import Dict, List, Optional, Set

from claudecube.pricing import DEFAULT_CONTEXT_WINDOW, context_window, message_cost

log = logging.getLogger(__name__)

SYNTHETIC_MODEL = "<synthetic>"
SYSTEM_PROMPT_SOURCES = {"system"}


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
    """Reads only the bytes appended since the last poll, so polling a large transcript stays cheap."""

    def __init__(self, path: Path) -> None:
        self._path = Path(path)
        self._offset = 0
        self._partial = b""
        self._acc = _Accumulator()

    @property
    def path(self) -> Path:
        return self._path

    def poll(self) -> TranscriptStats:
        for line in self._read_new_lines():
            entry = _parse(line)
            if entry is not None:
                _absorb(self._acc, entry)
        return _snapshot(self._acc)

    def _read_new_lines(self) -> List[bytes]:
        try:
            with self._path.open("rb") as fh:
                fh.seek(self._offset)
                chunk = fh.read()
        except OSError:
            return []
        self._offset += len(chunk)
        data = self._partial + chunk
        *lines, self._partial = data.split(b"\n")
        return lines


def _parse(line: bytes) -> Optional[dict]:
    if not line.strip():
        return None
    try:
        entry = json.loads(line)
    except ValueError:
        log.debug("Skipping malformed transcript line", extra={"length": len(line)})
        return None
    return entry if isinstance(entry, dict) else None


def _absorb(acc: _Accumulator, entry: dict) -> None:
    if acc.started_at is None:
        acc.started_at = _timestamp(entry.get("timestamp"))
    if isinstance(entry.get("cwd"), str):
        acc.cwd = entry["cwd"]

    kind = entry.get("type")
    if kind == "assistant":
        _absorb_assistant(acc, entry)
    elif kind == "user":
        _absorb_user(acc, entry)
    elif kind == "custom-title" and isinstance(entry.get("customTitle"), str):
        acc.title = entry["customTitle"]


def _absorb_assistant(acc: _Accumulator, entry: dict) -> None:
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

    for block in message.get("content") or []:
        if isinstance(block, dict) and block.get("type") == "tool_use" and isinstance(block.get("id"), str):
            acc.tool_ids.add(block["id"])

    if entry.get("isSidechain"):
        return
    acc.model, acc.speed = model, speed
    if isinstance(entry.get("effort"), str):
        acc.effort = entry["effort"]
    acc.context_tokens = sum(
        _int(usage.get(k)) for k in ("input_tokens", "cache_read_input_tokens", "cache_creation_input_tokens")
    )


def _absorb_user(acc: _Accumulator, entry: dict) -> None:
    result = entry.get("toolUseResult")
    if isinstance(result, dict):
        added, removed = _changed_lines(result)
        acc.lines_added += added
        acc.lines_removed += removed
        return
    if _is_real_prompt(entry):
        acc.turns += 1


def _is_real_prompt(entry: dict) -> bool:
    if entry.get("isMeta") or entry.get("promptSource") in SYSTEM_PROMPT_SOURCES:
        return False
    content = (entry.get("message") or {}).get("content")
    if isinstance(content, str):
        return not content.lstrip().startswith("<")
    if isinstance(content, list):
        kinds = {b.get("type") for b in content if isinstance(b, dict)}
        return "text" in kinds and "tool_result" not in kinds
    return False


def _changed_lines(result: dict) -> tuple[int, int]:
    added = removed = 0
    for hunk in result.get("structuredPatch") or []:
        for line in (hunk.get("lines") if isinstance(hunk, dict) else None) or []:
            if isinstance(line, str) and line.startswith("+"):
                added += 1
            elif isinstance(line, str) and line.startswith("-"):
                removed += 1
    if result.get("type") == "create" and isinstance(result.get("content"), str):
        added += len(result["content"].splitlines())
    return added, removed


def _snapshot(acc: _Accumulator) -> TranscriptStats:
    responses = list(acc.responses.values())
    costs = [message_cost(r.model, r.usage, r.speed) for r in responses]
    known = [c for c in costs if c is not None]
    return TranscriptStats(
        model=acc.model,
        effort=acc.effort,
        speed=acc.speed,
        title=acc.title,
        cwd=acc.cwd,
        started_at=acc.started_at,
        input_tokens=sum(_int(r.usage.get("input_tokens")) for r in responses),
        output_tokens=sum(_int(r.usage.get("output_tokens")) for r in responses),
        cache_read_tokens=sum(_int(r.usage.get("cache_read_input_tokens")) for r in responses),
        cache_write_tokens=sum(_int(r.usage.get("cache_creation_input_tokens")) for r in responses),
        context_tokens=acc.context_tokens,
        context_window=context_window(acc.model) if acc.model else DEFAULT_CONTEXT_WINDOW,
        cost_usd=sum(known) if known else None,
        turns=acc.turns,
        tool_calls=len(acc.tool_ids),
        lines_added=acc.lines_added,
        lines_removed=acc.lines_removed,
    )


def _timestamp(value: object) -> Optional[float]:
    if not isinstance(value, str):
        return None
    try:
        return datetime.fromisoformat(value.replace("Z", "+00:00")).timestamp()
    except ValueError:
        return None


def _int(value: object) -> int:
    return value if isinstance(value, int) and value > 0 else 0
