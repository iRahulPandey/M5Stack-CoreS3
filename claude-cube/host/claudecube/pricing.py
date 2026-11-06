"""API-equivalent prices and context windows for the models Claude Code runs on.

Prices are USD per million tokens, from the Claude API price list cached 2026-10-06.
Subscription plans are not billed per token, so the cube labels the figure "API-equivalent".
"""
from __future__ import annotations

import re
from dataclasses import dataclass
from typing import Mapping, Optional

MILLION = 1_000_000
WRITE_5M_MULTIPLIER = 1.25
WRITE_1H_MULTIPLIER = 2.0
DEFAULT_CACHE_READ_MULTIPLIER = 0.1
DEFAULT_CONTEXT_WINDOW = 200_000
HAIKU_5_5_LONG_PROMPT = 100_000
MAX_TOKENS = 100 * MILLION  # per response; anything bigger is a corrupt transcript line, not usage

_DATE_SUFFIX = re.compile(r"-\d{8}$")


@dataclass(frozen=True)
class Rates:
    input: float
    output: float
    cache_read: Optional[float] = None  # None means the default 0.1x input

    @property
    def read(self) -> float:
        return self.cache_read if self.cache_read is not None else self.input * DEFAULT_CACHE_READ_MULTIPLIER


@dataclass(frozen=True)
class ModelInfo:
    rates: Rates
    context_window: int = MILLION
    fast_multiplier: float = 1.0
    long_prompt_rates: Optional[Rates] = None


MODELS: Mapping[str, ModelInfo] = {
    "claude-fable-5-1": ModelInfo(Rates(10.0, 50.0, 0.25)),
    "claude-mythos-5-1": ModelInfo(Rates(10.0, 50.0, 0.25)),
    "claude-fable-5": ModelInfo(Rates(10.0, 50.0)),
    "claude-mythos-5": ModelInfo(Rates(10.0, 50.0)),
    "claude-opus-5-5": ModelInfo(Rates(4.0, 20.0, 0.20), fast_multiplier=2.0),
    "claude-opus-5": ModelInfo(Rates(5.0, 25.0), fast_multiplier=2.0),
    "claude-opus-4-8": ModelInfo(Rates(5.0, 25.0)),
    "claude-opus-4-7": ModelInfo(Rates(5.0, 25.0)),
    "claude-opus-4-6": ModelInfo(Rates(5.0, 25.0)),
    "claude-sonnet-5-5": ModelInfo(Rates(2.0, 10.0, 0.20)),
    "claude-sonnet-5": ModelInfo(Rates(2.0, 10.0)),
    "claude-sonnet-4-6": ModelInfo(Rates(3.0, 15.0)),
    "claude-haiku-5-5": ModelInfo(Rates(0.10, 0.50), long_prompt_rates=Rates(0.50, 2.50)),
    "claude-haiku-4-5": ModelInfo(Rates(1.0, 5.0), context_window=200_000),
}


def resolve_model(model_id: str) -> str:
    return _DATE_SUFFIX.sub("", model_id)


def display_name(model_id: str) -> str:
    """'claude-opus-5-5' -> 'Opus 5.5', 'claude-3-5-sonnet-20241022' -> 'Sonnet 3.5'."""
    parts = resolve_model(model_id).removeprefix("claude-").split("-")
    family = [p for p in parts if not p.isdigit()]
    version = [p for p in parts if p.isdigit()]
    if not family:
        return model_id
    name = " ".join(p.capitalize() for p in family)
    return f"{name} {'.'.join(version)}" if version else name


def context_window(model_id: str) -> int:
    info = MODELS.get(resolve_model(model_id))
    return info.context_window if info else DEFAULT_CONTEXT_WINDOW


def message_cost(model_id: str, usage: Mapping[str, object], speed: Optional[str]) -> Optional[float]:
    """Cost of one API response in USD, or None for a model with no known price."""
    info = MODELS.get(resolve_model(model_id))
    if info is None:
        return None

    input_tokens = _count(usage, "input_tokens")
    output_tokens = _count(usage, "output_tokens")
    read_tokens = _count(usage, "cache_read_input_tokens")
    write_5m, write_1h = _cache_writes(usage)

    rates = info.rates
    prompt_tokens = input_tokens + read_tokens + write_5m + write_1h
    if info.long_prompt_rates is not None and prompt_tokens > HAIKU_5_5_LONG_PROMPT:
        rates = info.long_prompt_rates

    dollars = (
        input_tokens * rates.input
        + output_tokens * rates.output
        + read_tokens * rates.read
        + write_5m * rates.input * WRITE_5M_MULTIPLIER
        + write_1h * rates.input * WRITE_1H_MULTIPLIER
    ) / MILLION
    return dollars * (info.fast_multiplier if speed == "fast" else 1.0)


def token_count(value: object) -> int:
    """A token count from a transcript, or 0 when it is not a plausible one."""
    if isinstance(value, bool) or not isinstance(value, int):
        return 0
    return value if 0 < value <= MAX_TOKENS else 0


def _count(usage: Mapping[str, object], key: str) -> int:
    return token_count(usage.get(key))


def _cache_writes(usage: Mapping[str, object]) -> tuple[int, int]:
    """Split cache writes by TTL; responses without the breakdown count as 5-minute writes."""
    total = _count(usage, "cache_creation_input_tokens")
    split = usage.get("cache_creation")
    if not isinstance(split, Mapping):
        return total, 0
    write_1h = _count(split, "ephemeral_1h_input_tokens")
    return max(total - write_1h, 0), write_1h
