from __future__ import annotations

import pytest

from claudecube.activity import ACTIVE_TIMEOUT_S, HookEvent, SessionBoard

SID = "0d07bbb1-8d2b-40a4-81fa-be0a6f707c6f"
OTHER_SID = "7f3c2a10-1b44-4e0e-9a51-2c8d0e6b9f21"
TRANSCRIPT = f"/Users/rahulpandey/.claude/projects/-Users-rahulpandey-Projects-issuetriage/{SID}.jsonl"


def event(
    name: str, ts: float, *, sid: str = SID, tool: str = "", detail: str = "", ntype: str = "", tuid: str = ""
) -> HookEvent:
    return HookEvent(
        ts=ts,
        event=name,
        session_id=sid,
        transcript_path=TRANSCRIPT,
        cwd="/Users/rahulpandey/Projects/issuetriage",
        tool=tool,
        detail=detail,
        notification_type=ntype,
        tool_use_id=tuid,
    )


def board_after(*events: HookEvent) -> SessionBoard:
    board = SessionBoard()
    for e in events:
        board.apply(e)
    return board


def state_after(*events: HookEvent, now: "float | None" = None) -> str:
    active = board_after(*events).active(now)
    assert active is not None
    return active.state


class TestStateTransitions:
    def test_should_be_thinking_after_a_prompt(self) -> None:
        assert state_after(event("UserPromptSubmit", 1.0)) == "thinking"

    def test_should_show_the_running_tool(self) -> None:
        board = SessionBoard()
        board.apply(event("PreToolUse", 1.0, tool="Bash", detail="Build firmware"))
        active = board.active()
        assert active is not None
        assert (active.state, active.tool, active.detail) == ("tool", "Bash", "Build firmware")

    def test_should_return_to_thinking_after_a_tool_finishes(self) -> None:
        assert state_after(event("PreToolUse", 1.0, tool="Read"), event("PostToolUse", 2.0, tool="Read")) == "thinking"

    def test_should_wait_on_a_permission_request(self) -> None:
        assert state_after(event("PermissionRequest", 1.0, tool="Bash")) == "waiting"

    def test_should_wait_on_a_permission_notification(self) -> None:
        assert state_after(event("Notification", 1.0, ntype="permission_prompt")) == "waiting"

    def test_should_hand_the_turn_back_on_an_idle_notification(self) -> None:
        assert state_after(event("Notification", 1.0, ntype="idle_prompt")) == "done"

    def test_should_ignore_unrelated_notifications(self) -> None:
        assert state_after(event("UserPromptSubmit", 1.0), event("Notification", 2.0, ntype="auth_success")) == "thinking"

    def test_should_be_done_when_claude_stops(self) -> None:
        assert state_after(event("UserPromptSubmit", 1.0), event("Stop", 2.0)) == "done"

    def test_should_flag_a_failed_stop(self) -> None:
        assert state_after(event("StopFailure", 1.0)) == "error"

    def test_should_mark_the_session_ended(self) -> None:
        assert state_after(event("SessionEnd", 1.0)) == "ended"


class TestOrdering:
    def test_should_ignore_events_older_than_the_last_one_applied(self) -> None:
        assert state_after(event("Stop", 5.0), event("PreToolUse", 4.0, tool="Bash")) == "done"

    def test_should_restart_the_state_clock_only_when_the_state_changes(self) -> None:
        board = SessionBoard()
        board.apply(event("UserPromptSubmit", 1.0))
        board.apply(event("PostToolUse", 3.0))
        active = board.active()
        assert active is not None
        assert active.state_since == 1.0


class TestToolUseIds:
    def test_should_finish_a_tool_whose_post_event_was_stamped_before_its_pre_event(self) -> None:
        pre = event("PreToolUse", 2.0, tool="Bash", tuid="toolu_01")
        post = event("PostToolUse", 1.9, tool="Bash", tuid="toolu_01")
        assert state_after(pre, post) == "thinking"

    def test_should_ignore_a_pre_event_that_arrives_after_its_post_event(self) -> None:
        post = event("PostToolUse", 1.9, tool="Bash", tuid="toolu_01")
        pre = event("PreToolUse", 2.0, tool="Bash", tuid="toolu_01")
        assert state_after(post, pre) == "thinking"

    def test_should_not_let_an_older_post_event_finish_another_tool(self) -> None:
        running = event("PreToolUse", 2.0, tool="Read", tuid="toolu_02")
        older = event("PostToolUse", 1.9, tool="Bash", tuid="toolu_01")
        assert state_after(running, older) == "tool"


class TestTimeouts:
    def test_should_keep_thinking_while_events_are_recent(self) -> None:
        assert state_after(event("UserPromptSubmit", 1_000.0), now=1_000.0 + ACTIVE_TIMEOUT_S - 1) == "thinking"

    def test_should_go_idle_after_thinking_with_no_events(self) -> None:
        board = board_after(event("UserPromptSubmit", 1_000.0))
        active = board.active(1_000.0 + ACTIVE_TIMEOUT_S + 5)
        assert active is not None
        assert (active.state, active.state_since) == ("idle", 1_000.0 + ACTIVE_TIMEOUT_S)

    def test_should_go_idle_after_a_tool_with_no_events(self) -> None:
        board = board_after(event("PreToolUse", 1_000.0, tool="Bash", detail="Build firmware"))
        active = board.active(1_000.0 + ACTIVE_TIMEOUT_S)
        assert active is not None
        assert (active.state, active.tool, active.detail) == ("idle", "", "")

    def test_should_keep_a_finished_turn_however_old(self) -> None:
        assert state_after(event("Stop", 1_000.0), now=1_000.0 + 86_400) == "done"


class TestActiveSession:
    def test_should_follow_the_most_recently_active_session(self) -> None:
        board = SessionBoard()
        board.apply(event("UserPromptSubmit", 1.0, sid=SID))
        board.apply(event("PreToolUse", 2.0, sid=OTHER_SID, tool="Edit"))
        active = board.active()
        assert active is not None
        assert active.session_id == OTHER_SID

    def test_should_have_no_active_session_before_any_event(self) -> None:
        assert SessionBoard().active() is None

    def test_should_parse_events_written_by_the_hook(self) -> None:
        raw = {"ts": 2.5, "event": "PreToolUse", "sid": SID, "transcript": TRANSCRIPT, "cwd": "/tmp", "tool": "Bash"}
        parsed = HookEvent.from_dict(raw)
        assert parsed is not None
        assert (parsed.event, parsed.tool, parsed.detail) == ("PreToolUse", "Bash", "")

    def test_should_reject_events_without_a_session(self) -> None:
        assert HookEvent.from_dict({"ts": 1.0, "event": "Stop"}) is None


@pytest.mark.parametrize(
    "ts", [float("nan"), float("inf"), -1.0, 1e300, 10 ** 400, True, "1.0"],
    ids=["nan", "infinity", "negative", "far future", "huge int", "bool", "string"],
)
def test_should_reject_events_stamped_with_an_impossible_time(ts: object) -> None:
    assert HookEvent.from_dict({"ts": ts, "event": "Stop", "sid": SID}) is None
