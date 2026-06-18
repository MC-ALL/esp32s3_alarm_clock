from __future__ import annotations

from datetime import datetime, timezone
from typing import Any

from ai_client import AIServiceError
from ai_storage import DialogMemoryItem, PresenceRuntimeState

DEVICE_CONNECTION_MAX_AGE_S = 30

KEY_EVENT_TYPES = {
    "todo_completed",
    "todo_deleted",
    "alarm_triggered",
    "rest_reminder_triggered",
    "env_alert_triggered",
    "welcome_played",
    "todo_sync_up_played",
}


def parse_iso_datetime(value: Any) -> datetime | None:
    if not isinstance(value, str) or not value.strip():
        return None
    try:
        return datetime.fromisoformat(value)
    except ValueError:
        return None


def is_device_connected(status: dict[str, Any], now: datetime | None = None) -> bool:
    if bool(status.get("online")) is not True:
        return False

    updated_at = parse_iso_datetime(status.get("updated_at"))
    if updated_at is None:
        return False

    current = now or datetime.now(timezone.utc).astimezone()
    age_s = (current - updated_at).total_seconds()
    return age_s >= -5 and age_s <= DEVICE_CONNECTION_MAX_AGE_S


def advance_presence_runtime(state: PresenceRuntimeState, status: dict[str, Any]) -> PresenceRuntimeState:
    updated_at = str(status.get("updated_at", "") or "")
    presence_detected = status.get("presence_detected")

    if updated_at and updated_at == state.last_status_updated_at and presence_detected == state.last_presence_detected:
        return state

    next_state = state.model_copy(deep=True)
    next_state.last_status_updated_at = updated_at

    if isinstance(presence_detected, bool):
        if presence_detected:
            if next_state.last_presence_detected is not True or not next_state.presence_session_started_at:
                next_state.presence_session_started_at = updated_at
        else:
            next_state.presence_session_started_at = None
        next_state.last_presence_detected = presence_detected

    return next_state


def build_continuous_presence_summary(status: dict[str, Any], runtime_state: PresenceRuntimeState) -> dict[str, Any]:
    updated_at = parse_iso_datetime(status.get("updated_at"))
    presence_detected = status.get("presence_detected")
    started_at = parse_iso_datetime(runtime_state.presence_session_started_at)

    if not isinstance(presence_detected, bool):
        return {
            "available": False,
            "presence_detected": None,
            "duration_s": None,
            "started_at": runtime_state.presence_session_started_at,
        }

    if not presence_detected:
        return {
            "available": True,
            "presence_detected": False,
            "duration_s": 0,
            "started_at": None,
        }

    if updated_at is None:
        return {
            "available": False,
            "presence_detected": True,
            "duration_s": None,
            "started_at": runtime_state.presence_session_started_at,
        }

    if started_at is None:
        started_at = updated_at

    duration_s = max(0, int((updated_at - started_at).total_seconds()))
    return {
        "available": True,
        "presence_detected": True,
        "duration_s": duration_s,
        "started_at": started_at.isoformat(timespec="seconds"),
    }


def build_config_summary(config: dict[str, Any]) -> dict[str, Any]:
    alarms = config.get("alarms", []) if isinstance(config.get("alarms"), list) else []
    enabled_alarms = [alarm for alarm in alarms if isinstance(alarm, dict) and bool(alarm.get("enabled"))]
    enabled_alarm_times = [
        f"{int(alarm.get('hour', 0)):02d}:{int(alarm.get('minute', 0)):02d}"
        for alarm in enabled_alarms
    ]
    return {
        "config_version": int(config.get("config_version", 0) or 0),
        "updated_at": str(config.get("updated_at", "") or ""),
        "alarm_count": len(alarms),
        "enabled_alarm_count": len(enabled_alarms),
        "enabled_alarm_times": enabled_alarm_times[:5],
        "voice_settings": config.get("voice_settings", {}),
    }


def build_open_todo_summary(items: list[dict[str, Any]], limit: int = 8) -> list[dict[str, Any]]:
    summary: list[dict[str, Any]] = []
    for item in items[:limit]:
        summary.append(
            {
                "id": str(item.get("id", "")),
                "text": str(item.get("text", "")),
                "updated_at": str(item.get("updated_at", "")),
            }
        )
    return summary


def build_report_event_summary(events: list[dict[str, Any]], limit: int = 10) -> dict[str, Any]:
    recent_items = [item for item in events if str(item.get("event_type", "")).strip() in KEY_EVENT_TYPES]
    recent_items = recent_items[-limit:]
    counts: dict[str, int] = {}
    compact_items: list[dict[str, Any]] = []

    for item in recent_items:
        event_type = str(item.get("event_type", "")).strip()
        counts[event_type] = counts.get(event_type, 0) + 1
        compact_items.append(
            {
                "event_at": str(item.get("event_at", "")),
                "event_type": event_type,
                "todo_id": item.get("todo_id"),
                "temperature_c": item.get("temperature_c"),
                "humidity_percent": item.get("humidity_percent"),
                "lux": item.get("lux"),
                "presence_detected": item.get("presence_detected"),
            }
        )

    return {
        "recent_event_count": len(compact_items),
        "event_type_counts": counts,
        "recent_events": compact_items,
    }


def _build_device_status_summary(status: dict[str, Any]) -> dict[str, Any]:
    if not is_device_connected(status):
        raise AIServiceError("请先连接闹钟", status_code=503)

    updated_at = str(status.get("updated_at", "") or "")

    return {
        "device_id": str(status.get("device_id", "") or ""),
        "updated_at": updated_at,
        "online": bool(status.get("online", False)),
        "temperature_c": status.get("temperature_c"),
        "humidity_percent": status.get("humidity_percent"),
        "lux": status.get("lux"),
        "presence_detected": status.get("presence_detected"),
    }


def build_chat_context(
    status: dict[str, Any],
    config: dict[str, Any],
    open_todos: list[dict[str, Any]],
    dialog_memory_items: list[DialogMemoryItem],
    runtime_state: PresenceRuntimeState,
) -> dict[str, Any]:
    return {
        "device_status": _build_device_status_summary(status),
        "config_summary": build_config_summary(config),
        "continuous_presence": build_continuous_presence_summary(status, runtime_state),
        "open_todos": build_open_todo_summary(open_todos),
        "dialog_memory_summaries": [item.summary_text for item in dialog_memory_items[-15:]],
    }


def build_report_context(
    status: dict[str, Any],
    config: dict[str, Any],
    open_todos: list[dict[str, Any]],
    event_history: list[dict[str, Any]],
    runtime_state: PresenceRuntimeState,
) -> dict[str, Any]:
    return {
        "device_status": _build_device_status_summary(status),
        "config_summary": build_config_summary(config),
        "continuous_presence": build_continuous_presence_summary(status, runtime_state),
        "open_todos": build_open_todo_summary(open_todos),
        "report_event_summary": build_report_event_summary(event_history),
    }
