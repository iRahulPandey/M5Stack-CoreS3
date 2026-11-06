from __future__ import annotations

import json
import os
from datetime import datetime, timezone
from pathlib import Path

import pytest

from claudecube.transcript import TranscriptReader

SESSION = "0d07bbb1-8d2b-40a4-81fa-be0a6f707c6f"


def assistant(
    msg_id: str,
    *,
    input_tokens: int = 2,
    output_tokens: int = 100,
    cache_read: int = 0,
    cache_write: int = 0,
    model: str = "claude-opus-5-5",
    sidechain: bool = False,
    effort: str = "xhigh",
    speed: str = "standard",
    content: list | None = None,
    ts: str = "2026-11-06T18:41:00.000Z",
) -> dict:
    return {
        "type": "assistant",
        "sessionId": SESSION,
        "isSidechain": sidechain,
        "effort": effort,
        "timestamp": ts,
        "message": {
            "id": msg_id,
            "model": model,
            "content": content or [{"type": "text", "text": "Checking the board."}],
            "usage": {
                "input_tokens": input_tokens,
                "output_tokens": output_tokens,
                "cache_read_input_tokens": cache_read,
                "cache_creation_input_tokens": cache_write,
                "cache_creation": {"ephemeral_5m_input_tokens": 0, "ephemeral_1h_input_tokens": cache_write},
                "speed": speed,
            },
        },
    }


def prompt(text: str, *, source: str = "sdk", ts: str = "2026-11-06T18:40:00.000Z", sidechain: bool = False) -> dict:
    return {
        "type": "user",
        "isSidechain": sidechain,
        "promptSource": source,
        "timestamp": ts,
        "message": {"role": "user", "content": text},
    }


def tool_result(result: dict, *, sidechain: bool = False) -> dict:
    return {
        "type": "user",
        "isSidechain": sidechain,
        "toolUseResult": result,
        "message": {"role": "user", "content": [{"type": "tool_result", "tool_use_id": "toolu_1", "content": "ok"}]},
    }


def write_lines(path: Path, entries: list[dict], *, extra: str = "") -> None:
    with path.open("a", encoding="utf-8") as fh:
        for entry in entries:
            fh.write(json.dumps(entry) + "\n")
        fh.write(extra)


@pytest.fixture
def transcript(tmp_path: Path) -> Path:
    return tmp_path / f"{SESSION}.jsonl"


@pytest.fixture
def subagent(transcript: Path) -> Path:
    """Where Claude Code writes a subagent's transcript: <session id>/subagents/ next to the main one."""
    agents = transcript.with_suffix("") / "subagents"
    agents.mkdir(parents=True)
    return agents / "agent-ace412394e3cf357e.jsonl"


class TestTokenTotals:
    def test_should_keep_the_last_copy_of_a_repeated_message_id(self, transcript: Path) -> None:
        write_lines(transcript, [
            assistant("msg_a", output_tokens=10),
            assistant("msg_a", output_tokens=250),
            assistant("msg_b", output_tokens=40, cache_read=1_000, cache_write=300),
        ])
        stats = TranscriptReader(transcript).poll()
        assert stats.output_tokens == 290
        assert stats.cache_read_tokens == 1_000
        assert stats.cache_write_tokens == 300
        assert stats.input_tokens == 4

    def test_should_sum_cost_across_messages(self, transcript: Path) -> None:
        write_lines(transcript, [assistant("msg_a", input_tokens=500_000, output_tokens=0)] * 2
                    + [assistant("msg_b", input_tokens=500_000, output_tokens=0)])
        assert TranscriptReader(transcript).poll().cost_usd == pytest.approx(4.0)


class TestContext:
    def test_should_measure_context_from_the_last_main_chain_request(self, transcript: Path, subagent: Path) -> None:
        write_lines(transcript, [
            assistant("msg_a", input_tokens=5, cache_read=90_000, cache_write=2_000),
            assistant("msg_b", input_tokens=3, cache_read=120_000, cache_write=800),
        ])
        write_lines(subagent, [assistant("msg_c", input_tokens=9, cache_read=4_000, sidechain=True)])
        stats = TranscriptReader(transcript).poll()
        assert stats.context_tokens == 120_803
        assert stats.context_window == 1_000_000


class TestSubagents:
    def test_should_add_subagent_tokens_cost_tool_calls_and_lines(self, transcript: Path, subagent: Path) -> None:
        call = {"type": "tool_use", "id": "toolu_9", "name": "Edit", "input": {}}
        write_lines(transcript, [assistant("msg_a", input_tokens=500_000, output_tokens=50)])
        write_lines(subagent, [
            prompt("Review the bridge for races", sidechain=True),
            assistant("msg_b", input_tokens=500_000, output_tokens=70, sidechain=True, content=[call]),
            tool_result({"structuredPatch": [{"lines": ["+one", "+two", "-three"]}]}, sidechain=True),
        ])
        stats = TranscriptReader(transcript).poll()
        assert (stats.input_tokens, stats.output_tokens) == (1_000_000, 120)
        assert stats.cost_usd == pytest.approx(4.0 + 120 * 20 / 1_000_000)
        assert stats.tool_calls == 1
        assert (stats.lines_added, stats.lines_removed) == (2, 1)

    def test_should_keep_model_effort_title_and_turns_from_the_main_session(self, transcript: Path, subagent: Path) -> None:
        write_lines(transcript, [
            prompt("fix the review findings"),
            assistant("msg_a", model="claude-opus-5-5", effort="xhigh"),
            {"type": "custom-title", "customTitle": "Claude cube review fixes"},
        ])
        write_lines(subagent, [
            prompt("Find the subagent transcripts", sidechain=True),
            assistant("msg_b", model="claude-haiku-5-5", effort="low", speed="fast", sidechain=True),
        ])
        stats = TranscriptReader(transcript).poll()
        assert (stats.model, stats.effort, stats.speed) == ("claude-opus-5-5", "xhigh", "standard")
        assert (stats.title, stats.turns) == ("Claude cube review fixes", 1)

    def test_should_pick_up_a_subagent_that_starts_later(self, transcript: Path, subagent: Path) -> None:
        reader = TranscriptReader(transcript)
        write_lines(transcript, [assistant("msg_a", output_tokens=10)])
        assert reader.poll().output_tokens == 10
        write_lines(subagent, [assistant("msg_b", output_tokens=5, sidechain=True)])
        assert reader.poll().output_tokens == 15
        write_lines(subagent, [assistant("msg_c", output_tokens=1, sidechain=True)])
        assert reader.poll().output_tokens == 16


class TestSessionFacts:
    def test_should_report_latest_model_effort_and_speed(self, transcript: Path) -> None:
        write_lines(transcript, [
            assistant("msg_a", model="claude-sonnet-5-5", effort="high"),
            assistant("msg_b", model="claude-opus-5-5", effort="xhigh", speed="fast"),
            assistant("msg_c", model="<synthetic>"),
        ])
        stats = TranscriptReader(transcript).poll()
        assert (stats.model, stats.effort, stats.speed) == ("claude-opus-5-5", "xhigh", "fast")

    def test_should_use_the_latest_custom_title(self, transcript: Path) -> None:
        write_lines(transcript, [
            {"type": "custom-title", "customTitle": "ESP32 device connection"},
            {"type": "custom-title", "customTitle": "Claude cube status display"},
        ])
        assert TranscriptReader(transcript).poll().title == "Claude cube status display"

    def test_should_take_the_start_time_from_the_first_timestamp(self, transcript: Path) -> None:
        write_lines(transcript, [prompt("hey claude check the esp32", ts="2026-11-06T18:41:12.500Z"), assistant("msg_a")])
        expected = datetime(2026, 11, 6, 18, 41, 12, 500_000, tzinfo=timezone.utc).timestamp()
        assert TranscriptReader(transcript).poll().started_at == pytest.approx(expected)


class TestActivityCounts:
    def test_should_count_only_real_prompts_as_turns(self, transcript: Path) -> None:
        write_lines(transcript, [
            prompt("read my project ideas"),
            prompt("<task-notification>backup done</task-notification>", source="system"),
            {"type": "user", "isMeta": True, "message": {"role": "user", "content": "caveat"}},
            tool_result({"stdout": "ok"}),
            prompt("yes, smoke test"),
        ])
        assert TranscriptReader(transcript).poll().turns == 2

    def test_should_count_unique_tool_calls(self, transcript: Path) -> None:
        call = {"type": "tool_use", "id": "toolu_1", "name": "Bash", "input": {}}
        other = {"type": "tool_use", "id": "toolu_2", "name": "Read", "input": {}}
        write_lines(transcript, [
            assistant("msg_a", content=[call]),
            assistant("msg_a", content=[call, other]),
        ])
        assert TranscriptReader(transcript).poll().tool_calls == 2

    def test_should_count_changed_lines_from_patches_and_new_files(self, transcript: Path) -> None:
        patch = {"structuredPatch": [{"lines": [" keep", "+added one", "+added two", "-removed"]}]}
        created = {"type": "create", "content": "line 1\nline 2\nline 3\n", "structuredPatch": []}
        write_lines(transcript, [tool_result(patch), tool_result(created)])
        stats = TranscriptReader(transcript).poll()
        assert (stats.lines_added, stats.lines_removed) == (5, 1)


class TestIncrementalReads:
    def test_should_pick_up_appended_lines_on_the_next_poll(self, transcript: Path) -> None:
        reader = TranscriptReader(transcript)
        write_lines(transcript, [assistant("msg_a", output_tokens=10)])
        assert reader.poll().output_tokens == 10
        write_lines(transcript, [assistant("msg_b", output_tokens=15)])
        assert reader.poll().output_tokens == 25

    def test_should_wait_for_a_partial_line_to_finish(self, transcript: Path) -> None:
        reader = TranscriptReader(transcript)
        line = json.dumps(assistant("msg_a", output_tokens=33))
        write_lines(transcript, [], extra=line[:40])
        assert reader.poll().output_tokens == 0
        write_lines(transcript, [], extra=line[40:] + "\n")
        assert reader.poll().output_tokens == 33

    def test_should_skip_malformed_lines(self, transcript: Path) -> None:
        write_lines(transcript, [assistant("msg_a", output_tokens=7)], extra="{not json\n")
        assert TranscriptReader(transcript).poll().output_tokens == 7

    def test_should_start_over_when_the_transcript_is_truncated(self, transcript: Path) -> None:
        reader = TranscriptReader(transcript)
        write_lines(transcript, [assistant("msg_a", output_tokens=10), assistant("msg_b", output_tokens=15)])
        assert reader.poll().output_tokens == 25
        transcript.write_text(json.dumps(assistant("msg_c", output_tokens=7)) + "\n", encoding="utf-8")
        assert reader.poll().output_tokens == 7

    def test_should_start_over_when_the_transcript_is_replaced(self, transcript: Path) -> None:
        reader = TranscriptReader(transcript)
        write_lines(transcript, [assistant("msg_a", output_tokens=10)])
        assert reader.poll().output_tokens == 10
        replacement = transcript.with_name("replacement.jsonl")
        write_lines(replacement, [assistant(f"msg_{n}", output_tokens=1) for n in range(5)])
        os.replace(replacement, transcript)
        assert reader.poll().output_tokens == 5

    def test_should_return_empty_stats_for_a_missing_file(self, tmp_path: Path) -> None:
        stats = TranscriptReader(tmp_path / "missing.jsonl").poll()
        assert stats.output_tokens == 0
        assert stats.model is None
