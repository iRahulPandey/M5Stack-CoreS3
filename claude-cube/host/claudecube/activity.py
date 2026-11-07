"""What each Claude Code session is doing, rebuilt from hook events."""
from __future__ import annotations

from collections import deque
from dataclasses import dataclass, replace
from typing import Deque, Dict, Mapping, Optional

MAX_TS = 4_102_444_800  # 2100-01-01; the hook stamps time.time(), so anything later is corrupt
ACTIVE_TIMEOUT_S = 10 * 60  # Bash's longest timeout; quieter than this, Claude was interrupted or killed
STATE_TIMEOUTS_S: Mapping[str, float] = {"thinking": ACTIVE_TIMEOUT_S, "tool": ACTIVE_TIMEOUT_S}
WAITING_PRIORITY_S = 60 * 60  # unanswered this long, a wait is probably a closed terminal: stop putting it first
TOOL_DONE_EVENTS = {"PostToolUse", "PostToolUseFailure"}
FINISHED_TOOLS_KEPT = 64  # per session, to spot a PreToolUse that arrives after its own PostToolUse

WAITING_NOTIFICATIONS = {"permission_prompt", "elicitation_dialog", "elicitation_url_dialog", "agent_needs_input"}
DONE_NOTIFICATIONS = {"idle_prompt"}

# Fixed state for each hook event; Notification and the tool events are handled separately.
EVENT_STATES: Mapping[str, str] = {
    "SessionStart": "idle",
    "UserPromptSubmit": "thinking",
    "PostToolUse": "thinking",
    "PostToolUseFailure": "thinking",
    "PermissionRequest": "waiting",
    "Stop": "done",
    "StopFailure": "error",
    "SessionEnd": "ended",
}


@dataclass(frozen=True)
class HookEvent:
    ts: float
    event: str
    session_id: str
    transcript_path: str
    cwd: str
    tool: str = ""
    detail: str = ""
    notification_type: str = ""
    tool_use_id: str = ""

    @staticmethod
    def from_dict(raw: Mapping[str, object]) -> Optional["HookEvent"]:
        """Build an event from one line the hook wrote; None when required fields are missing."""
        ts, name, sid = raw.get("ts"), raw.get("event"), raw.get("sid")
        if not _plausible_time(ts) or not isinstance(name, str) or not isinstance(sid, str) or not sid:
            return None
        return HookEvent(
            ts=float(ts),
            event=name,
            session_id=sid,
            transcript_path=_str(raw.get("transcript")),
            cwd=_str(raw.get("cwd")),
            tool=_str(raw.get("tool")),
            detail=_str(raw.get("detail")),
            notification_type=_str(raw.get("ntype")),
            tool_use_id=_str(raw.get("tuid")),
        )


@dataclass(frozen=True)
class SessionActivity:
    session_id: str
    transcript_path: str
    cwd: str
    state: str
    tool: str
    detail: str
    state_since: float
    last_ts: float
    tool_use_id: str = ""


class SessionBoard:
    """Latest activity per session. Hooks run in the background, so events can arrive out of order."""

    def __init__(self) -> None:
        self._sessions: Dict[str, SessionActivity] = {}
        self._finished: Dict[str, Deque[str]] = {}

    def apply(self, event: HookEvent) -> None:
        current = self._sessions.get(event.session_id)
        if self._is_stale(event, current):
            return
        next_state = _state_for(event)
        if next_state is None:
            return
        self._sessions[event.session_id] = _advance(current, event, *next_state)

    def active(self, now: Optional[float] = None) -> Optional[SessionActivity]:
        """The session to show: one waiting on the user first, else the latest. With `now`, silent activity times out."""
        sessions = [_settled(s, now) if now is not None else s for s in self._sessions.values()]
        waiting = [s for s in sessions if s.state == "waiting" and (now is None or now - s.last_ts < WAITING_PRIORITY_S)]
        return max(waiting or sessions, key=lambda s: s.last_ts, default=None)

    def last_event_ts(self) -> Optional[float]:
        active = self.active()
        return active.last_ts if active else None

    def _is_stale(self, event: HookEvent, current: Optional[SessionActivity]) -> bool:
        """Each hook stamps its own time as it starts, so a PostToolUse can be stamped before its PreToolUse."""
        finished = self._finished.setdefault(event.session_id, deque(maxlen=FINISHED_TOOLS_KEPT))
        if event.event == "PreToolUse" and event.tool_use_id and event.tool_use_id in finished:
            return True  # its PostToolUse already arrived
        if event.event in TOOL_DONE_EVENTS and event.tool_use_id:
            finished.append(event.tool_use_id)
        if current is None or event.ts >= current.last_ts:
            return False
        ends_current_tool = event.event in TOOL_DONE_EVENTS and bool(event.tool_use_id) and event.tool_use_id == current.tool_use_id
        return not ends_current_tool


def _advance(current: Optional[SessionActivity], event: HookEvent, state: str, tool: str, detail: str) -> SessionActivity:
    tool_use_id = event.tool_use_id if state == "tool" else ""
    if current is None:
        return SessionActivity(
            session_id=event.session_id,
            transcript_path=event.transcript_path,
            cwd=event.cwd,
            state=state,
            tool=tool,
            detail=detail,
            state_since=event.ts,
            last_ts=event.ts,
            tool_use_id=tool_use_id,
        )
    changed = (state, tool) != (current.state, current.tool)
    return replace(
        current,
        transcript_path=event.transcript_path or current.transcript_path,
        cwd=event.cwd or current.cwd,
        state=state,
        tool=tool,
        detail=detail,
        state_since=event.ts if changed else current.state_since,
        last_ts=max(event.ts, current.last_ts),
        tool_use_id=tool_use_id,
    )


def _settled(activity: SessionActivity, now: float) -> SessionActivity:
    """Thinking or a tool with no events for too long means Esc or a killed terminal, which send no hook."""
    timeout = STATE_TIMEOUTS_S.get(activity.state)
    if timeout is None or now - activity.last_ts < timeout:
        return activity
    return replace(activity, state="idle", tool="", detail="", tool_use_id="", state_since=activity.last_ts + timeout)


def _state_for(event: HookEvent) -> Optional[tuple[str, str, str]]:
    """(state, tool, detail) after this event, or None when the event changes nothing."""
    if event.event == "PreToolUse":
        return "tool", event.tool, event.detail
    if event.event == "PermissionRequest":
        return "waiting", event.tool, event.detail
    if event.event == "Notification":
        if event.notification_type in WAITING_NOTIFICATIONS:
            return "waiting", "", ""
        if event.notification_type in DONE_NOTIFICATIONS:
            return "done", "", ""
        return None
    state = EVENT_STATES.get(event.event)
    return (state, "", "") if state else None


def _plausible_time(ts: object) -> bool:
    """A Unix time the hook could have stamped. The range check also rejects NaN and the infinities."""
    return isinstance(ts, (int, float)) and not isinstance(ts, bool) and 0 <= ts <= MAX_TS


def _str(value: object) -> str:
    return value if isinstance(value, str) else ""
