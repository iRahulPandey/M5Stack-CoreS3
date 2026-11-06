"""The one-line JSON frame the bridge sends to the cube over USB serial."""
from __future__ import annotations

import json
from pathlib import Path
from typing import Optional

from claudecube.activity import SessionActivity
from claudecube.pricing import display_name
from claudecube.transcript import TranscriptStats

FRAME_VERSION = 1
MAX_TITLE = 48
MAX_TEXT = 40
TICKING_FIELDS = {"seq", "el", "sfor"}


def build_frame(activity: Optional[SessionActivity], stats: TranscriptStats, now: float, seq: int) -> dict:
    cwd = (activity.cwd if activity else "") or stats.cwd or ""
    project = Path(cwd).name if cwd else ""
    return {
        "v": FRAME_VERSION,
        "seq": seq,
        "st": activity.state if activity else "idle",
        "tool": _clip(activity.tool if activity else "", MAX_TEXT),
        "det": _clip(activity.detail if activity else "", MAX_TEXT),
        "sfor": _seconds(now - activity.state_since) if activity else 0,
        "proj": _clip(project, MAX_TEXT),
        "title": _clip(stats.title or project, MAX_TITLE),
        "model": _clip(display_name(stats.model), MAX_TEXT) if stats.model else "",
        "mid": _clip(stats.model or "", MAX_TEXT),
        "eff": _clip(stats.effort or "", 12),
        "fast": 1 if stats.speed == "fast" else 0,
        "tin": stats.input_tokens,
        "tout": stats.output_tokens,
        "tcr": stats.cache_read_tokens,
        "tcw": stats.cache_write_tokens,
        "ctx": stats.context_tokens,
        "ctxmax": stats.context_window,
        "cost": round(stats.cost_usd, 2) if stats.cost_usd is not None else -1,
        "el": _seconds(now - stats.started_at) if stats.started_at else 0,
        "turns": stats.turns,
        "tools": stats.tool_calls,
        "la": stats.lines_added,
        "lr": stats.lines_removed,
    }


def significant(frame: dict) -> dict:
    """The frame without fields that change every second, for deciding whether to send now."""
    return {k: v for k, v in frame.items() if k not in TICKING_FIELDS}


def encode_frame(frame: dict) -> bytes:
    return (json.dumps(frame, ensure_ascii=True, separators=(",", ":")) + "\n").encode("ascii")


def _clip(text: str, limit: int) -> str:
    flat = " ".join(text.split())
    return flat if len(flat) <= limit else flat[: limit - 1] + "~"


def _seconds(delta: float) -> int:
    return max(int(delta), 0)
