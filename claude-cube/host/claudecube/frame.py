"""The one-line JSON frame the bridge sends to the cube over USB serial."""
from __future__ import annotations

import json
import math
import unicodedata
from pathlib import Path
from typing import Optional

from claudecube.activity import SessionActivity
from claudecube.pricing import display_name
from claudecube.transcript import TranscriptStats

FRAME_VERSION = 1
MAX_TITLE = 48
MAX_TEXT = 40
TICKING_FIELDS = {"seq", "el", "sfor"}
FIRMWARE_LINE_MAX = 1024  # FRAME_LINE_MAX in firmware/src/frameLink.h; longer lines are dropped
UINT32_MAX = 2 ** 32 - 1  # the firmware keeps times and counts in uint32_t
MAX_TOKENS = 2 ** 53 - 1  # token counts are uint64_t there; this keeps them exact and at most 16 digits
MAX_COST = 999_999.99
# The cube's fonts are ASCII only. These have no ASCII decomposition, so map them to look-alikes.
ASCII_LOOKALIKES = str.maketrans({
    "\u2018": "'", "\u2019": "'", "\u201a": "'", "\u201c": '"', "\u201d": '"', "\u201e": '"',
    "\u2013": "-", "\u2014": "-", "\u2212": "-", "\u2044": "/", "\u2022": "*", "\u00d7": "x",
    "\u2190": "<-", "\u2192": "->",
})
INVISIBLE_CATEGORIES = {"Cc", "Cf", "Mn", "Me"}  # control and format characters, combining marks


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
        "tin": _count(stats.input_tokens, MAX_TOKENS),
        "tout": _count(stats.output_tokens, MAX_TOKENS),
        "tcr": _count(stats.cache_read_tokens, MAX_TOKENS),
        "tcw": _count(stats.cache_write_tokens, MAX_TOKENS),
        "ctx": _count(stats.context_tokens, MAX_TOKENS),
        "ctxmax": _count(stats.context_window, MAX_TOKENS),
        "cost": _cost(stats.cost_usd),
        "el": _seconds(now - stats.started_at) if stats.started_at else 0,
        "turns": _count(stats.turns, UINT32_MAX),
        "tools": _count(stats.tool_calls, UINT32_MAX),
        "la": _count(stats.lines_added, UINT32_MAX),
        "lr": _count(stats.lines_removed, UINT32_MAX),
    }


def significant(frame: dict) -> dict:
    """The frame without fields that change every second, for deciding whether to send now."""
    return {k: v for k, v in frame.items() if k not in TICKING_FIELDS}


def encode_frame(frame: dict) -> bytes:
    return (json.dumps(frame, ensure_ascii=True, separators=(",", ":")) + "\n").encode("ascii")


def _clip(text: str, limit: int) -> str:
    flat = " ".join(_ascii(text).split())
    return flat if len(flat) <= limit else flat[: limit - 1] + "~"


def _ascii(text: str) -> str:
    """What the cube can draw: accents dropped, typographic punctuation simplified, '?' for anything else.

    Clipping after this keeps each field to its byte budget; escaped non-ASCII would take 6 to 12 bytes a character.
    """
    out = []
    for ch in unicodedata.normalize("NFKD", text).translate(ASCII_LOOKALIKES):
        if ch.isspace():
            out.append(" ")
        elif " " < ch <= "~":
            out.append(ch)
        elif unicodedata.category(ch) not in INVISIBLE_CATEGORIES:
            out.append("?")
    return "".join(out)


def _count(value: int, limit: int) -> int:
    return min(max(value, 0), limit)


def _cost(dollars: Optional[float]) -> float:
    if dollars is None or not math.isfinite(dollars):
        return -1
    return round(min(max(dollars, 0.0), MAX_COST), 2)


def _seconds(delta: float) -> int:
    return int(min(max(delta, 0.0), UINT32_MAX)) if math.isfinite(delta) else 0
