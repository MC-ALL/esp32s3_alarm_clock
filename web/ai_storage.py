from __future__ import annotations

import json
from pathlib import Path
from typing import Any, Optional

from pydantic import BaseModel, Field, ValidationError

from ai_client import AIServiceError


class DialogMemoryItem(BaseModel):
    id: str
    created_at: str
    summary_text: str
    source_user_input_excerpt: str = ""
    source_answer_excerpt: str = ""


class PresenceRuntimeState(BaseModel):
    last_presence_detected: Optional[bool] = None
    presence_session_started_at: Optional[str] = None
    last_status_updated_at: str = ""


class HealthReportRecord(BaseModel):
    generated_at: str
    render_version: str = "v1"
    input_snapshot: dict[str, Any] = Field(default_factory=dict)
    report_fields: dict[str, Any] = Field(default_factory=dict)


def _read_json_file(path: Path, default: Any) -> Any:
    if not path.exists():
        return default
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        raise AIServiceError(f"{path.name} is invalid JSON", status_code=500) from exc
    except OSError as exc:
        raise AIServiceError(f"failed to read {path.name}", status_code=500) from exc


def _write_json_file(path: Path, payload: Any) -> None:
    temp_file = path.with_suffix(path.suffix + ".tmp")
    body = json.dumps(payload, ensure_ascii=False, indent=2) + "\n"
    try:
        path.parent.mkdir(parents=True, exist_ok=True)
        temp_file.write_text(body, encoding="utf-8")
        temp_file.replace(path)
    except OSError as exc:
        raise AIServiceError(f"failed to write {path.name}", status_code=500) from exc


def ensure_ai_storage(memory_file: Path, runtime_state_file: Path) -> None:
    memory_file.parent.mkdir(parents=True, exist_ok=True)
    if not memory_file.exists():
        _write_json_file(memory_file, [])
    if not runtime_state_file.exists():
        _write_json_file(runtime_state_file, PresenceRuntimeState().model_dump())


def read_dialog_memory(memory_file: Path) -> list[DialogMemoryItem]:
    raw = _read_json_file(memory_file, [])
    if not isinstance(raw, list):
        raise AIServiceError(f"{memory_file.name} root must be a JSON array", status_code=500)

    items: list[DialogMemoryItem] = []
    for entry in raw:
        try:
            items.append(DialogMemoryItem.model_validate(entry))
        except ValidationError as exc:
            raise AIServiceError(f"{memory_file.name} contains invalid memory item", status_code=500) from exc
    return items


def write_dialog_memory(memory_file: Path, items: list[DialogMemoryItem]) -> None:
    _write_json_file(memory_file, [item.model_dump() for item in items])


def append_dialog_memory(memory_file: Path, item: DialogMemoryItem) -> None:
    items = read_dialog_memory(memory_file)
    items.append(item)
    write_dialog_memory(memory_file, items)


def clear_dialog_memory(memory_file: Path) -> None:
    write_dialog_memory(memory_file, [])


def read_presence_runtime(runtime_state_file: Path) -> PresenceRuntimeState:
    raw = _read_json_file(runtime_state_file, PresenceRuntimeState().model_dump())
    try:
        return PresenceRuntimeState.model_validate(raw)
    except ValidationError as exc:
        raise AIServiceError(f"{runtime_state_file.name} contains invalid runtime state", status_code=500) from exc


def write_presence_runtime(runtime_state_file: Path, state: PresenceRuntimeState) -> None:
    _write_json_file(runtime_state_file, state.model_dump())


def read_latest_report(report_file: Path) -> Optional[HealthReportRecord]:
    if not report_file.exists():
        return None

    raw = _read_json_file(report_file, None)
    if raw is None:
        return None

    try:
        return HealthReportRecord.model_validate(raw)
    except ValidationError as exc:
        raise AIServiceError(f"{report_file.name} contains invalid report data", status_code=500) from exc


def write_latest_report(report_file: Path, report: HealthReportRecord) -> None:
    _write_json_file(report_file, report.model_dump())
