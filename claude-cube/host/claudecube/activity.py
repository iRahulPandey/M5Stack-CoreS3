"""What each Claude Code session is doing, rebuilt from hook events."""
from __future__ import annotations

from dataclasses import dataclass, replace
from typing import Dict, Mapping, Optional

MAX_TS = 4_102_444_800  # 2100-01-01; the hook stamps time.time(), so anything later is corrupt

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


class SessionBoard:
    """Latest activity per session. Hooks run in the background, so events can arrive out of order."""

    def __init__(self) -> None:
        self._sessions: Dict[str, SessionActivity] = {}

    def apply(self, event: HookEvent) -> None:
        current = self._sessions.get(event.session_id)
        if current is not None and event.ts < current.last_ts:
            return
        next_state = _state_for(event)
        if next_state is None:
            return
        state, tool, detail = next_state
        if current is None:
            self._sessions[event.session_id] = SessionActivity(
                session_id=event.session_id,
                transcript_path=event.transcript_path,
                cwd=event.cwd,
                state=state,
                tool=tool,
                detail=detail,
                state_since=event.ts,
                last_ts=event.ts,
            )
            return
        changed = (state, tool) != (current.state, current.tool)
        self._sessions[event.session_id] = replace(
            current,
            transcript_path=event.transcript_path or current.transcript_path,
            cwd=event.cwd or current.cwd,
            state=state,
            tool=tool,
            detail=detail,
            state_since=event.ts if changed else current.state_since,
            last_ts=event.ts,
        )

    def active(self) -> Optional[SessionActivity]:
        if not self._sessions:
            return None
        return max(self._sessions.values(), key=lambda s: s.last_ts)

    def last_event_ts(self) -> Optional[float]:
        active = self.active()
        return active.last_ts if active else None


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
