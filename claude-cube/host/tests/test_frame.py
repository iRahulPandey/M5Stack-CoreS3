from __future__ import annotations

import json
from dataclasses import replace

from claudecube.activity import SessionActivity
from claudecube.frame import build_frame, encode_frame, significant
from claudecube.transcript import TranscriptStats

ACTIVITY = SessionActivity(
    session_id="0d07bbb1-8d2b-40a4-81fa-be0a6f707c6f",
    transcript_path="/tmp/0d07bbb1.jsonl",
    cwd="/Users/rahulpandey/Projects/issuetriage",
    state="tool",
    tool="Bash",
    detail="Build firmware",
    state_since=1_000.0,
    last_ts=1_010.0,
)

STATS = TranscriptStats(
    model="claude-opus-5-5",
    effort="xhigh",
    speed="standard",
    title="ESP32 device connection",
    cwd="/Users/rahulpandey/Projects/issuetriage",
    started_at=100.0,
    input_tokens=1_240,
    output_tokens=31_800,
    cache_read_tokens=1_190_000,
    cache_write_tokens=142_000,
    context_tokens=184_000,
    context_window=1_000_000,
    cost_usd=4.125,
    turns=18,
    tool_calls=64,
    lines_added=412,
    lines_removed=36,
)


class TestBuildFrame:
    def test_should_carry_activity_and_session_numbers(self) -> None:
        frame = build_frame(ACTIVITY, STATS, now=1_042.0, seq=7)
        assert frame["st"] == "tool"
        assert frame["tool"] == "Bash"
        assert frame["model"] == "Opus 5.5"
        assert frame["tout"] == 31_800
        assert frame["ctx"] == 184_000
        assert frame["ctxmax"] == 1_000_000
        assert frame["seq"] == 7

    def test_should_report_seconds_since_start_and_in_state(self) -> None:
        frame = build_frame(ACTIVITY, STATS, now=1_042.0, seq=1)
        assert (frame["el"], frame["sfor"]) == (942, 42)

    def test_should_send_minus_one_when_cost_is_unknown(self) -> None:
        frame = build_frame(ACTIVITY, replace(STATS, cost_usd=None), now=1_042.0, seq=1)
        assert frame["cost"] == -1

    def test_should_fall_back_to_the_project_name_without_a_title(self) -> None:
        frame = build_frame(ACTIVITY, replace(STATS, title=None), now=1_042.0, seq=1)
        assert frame["title"] == "issuetriage"

    def test_should_be_idle_without_hook_activity(self) -> None:
        frame = build_frame(None, STATS, now=1_042.0, seq=1)
        assert (frame["st"], frame["proj"]) == ("idle", "issuetriage")

    def test_should_flag_fast_mode(self) -> None:
        assert build_frame(ACTIVITY, replace(STATS, speed="fast"), now=1_042.0, seq=1)["fast"] == 1


class TestSignificant:
    def test_should_ignore_ticking_fields_when_comparing_frames(self) -> None:
        early = build_frame(ACTIVITY, STATS, now=1_042.0, seq=1)
        later = build_frame(ACTIVITY, STATS, now=1_050.0, seq=2)
        assert significant(early) == significant(later)

    def test_should_notice_a_state_change(self) -> None:
        before = build_frame(ACTIVITY, STATS, now=1_042.0, seq=1)
        after = build_frame(replace(ACTIVITY, state="waiting"), STATS, now=1_042.0, seq=1)
        assert significant(before) != significant(after)


class TestEncodeFrame:
    def test_should_fit_one_short_line(self) -> None:
        long_title = replace(STATS, title="A very long session title " * 10)
        line = encode_frame(build_frame(ACTIVITY, long_title, now=1_042.0, seq=1))
        assert line.endswith(b"\n")
        assert line.count(b"\n") == 1
        assert len(line) < 512
        assert json.loads(line)["title"].startswith("A very long")
