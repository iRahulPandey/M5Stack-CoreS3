from __future__ import annotations

import json

from hook import summarize

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
