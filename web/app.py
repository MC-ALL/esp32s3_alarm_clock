from __future__ import annotations

import json
import os
import uuid
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from fastapi import FastAPI, HTTPException, Query
from fastapi.responses import HTMLResponse, JSONResponse, RedirectResponse, Response
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel
from starlette.requests import Request
from starlette.templating import Jinja2Templates

from ai_chat import generate_chat_response
from ai_client import AIServiceError, load_ai_settings_from_env
from ai_context import advance_presence_runtime, build_chat_context, build_report_context
from ai_pdf import build_health_report_pdf
from ai_report import generate_health_report
from ai_storage import (
    clear_dialog_memory,
    ensure_ai_storage,
    append_dialog_memory,
    read_dialog_memory,
    read_latest_report,
    read_presence_runtime,
    write_latest_report,
    write_presence_runtime,
)

BASE_DIR = Path(__file__).resolve().parent
DATA_DIR = Path(os.getenv("CLOCK_WEB_DATA_DIR", str(BASE_DIR / "data")))
LEGACY_TODOS_FILE = DATA_DIR / "todos.json"
ACTIVE_TODOS_FILE = DATA_DIR / "todos_active.json"
COMPLETED_TODOS_FILE = DATA_DIR / "todos_completed.json"
DEVICE_STATUS_FILE = DATA_DIR / "device_status.json"
EVENT_HISTORY_FILE = DATA_DIR / "event_history.json"
DEVICE_CONFIG_FILE = DATA_DIR / "device_config.json"
MODEL_DIALOG_MEMORY_FILE = DATA_DIR / "model_dialog_memory.json"
MODEL_PRESENCE_RUNTIME_FILE = DATA_DIR / "model_presence_runtime.json"
LATEST_HEALTH_REPORT_FILE = DATA_DIR / "latest_health_report.json"
MAX_TEXT_LENGTH = 96
MAX_EVENT_HISTORY = 500
DEFAULT_DEVICE_ID = "clock-001"

app = FastAPI(title="Clock Control Center")
app.mount("/static", StaticFiles(directory=BASE_DIR / "static"), name="static")
templates = Jinja2Templates(directory=str(BASE_DIR / "templates"))


@app.exception_handler(AIServiceError)
async def ai_service_error_handler(_: Request, exc: AIServiceError) -> JSONResponse:
    return JSONResponse(status_code=exc.status_code, content={"detail": exc.detail})


class CreateTodoRequest(BaseModel):
    text: str


class UpdateTodoRequest(BaseModel):
    text: str | None = None
    done: bool | None = None


class ReorderTodosRequest(BaseModel):
    ids: list[str]


class AlarmPayload(BaseModel):
    hour: int
    minute: int
    repeat: bool
    enabled: bool
    voice: bool


class VoiceSettingsPayload(BaseModel):
    todo_voice_on: bool
    alarm_voice_on: bool
    env_voice_on: bool
    env_alert_on: bool
    home_hour_chime_on: bool


class DeviceAlarmsRequest(BaseModel):
    config_version: int | None = None
    alarms: list[AlarmPayload]


class DeviceVoiceSettingsRequest(BaseModel):
    config_version: int | None = None
    voice_settings: VoiceSettingsPayload


class DeviceConfigUpdateRequest(BaseModel):
    config_version: int | None = None
    alarms: list[AlarmPayload] | None = None
    voice_settings: VoiceSettingsPayload | None = None


class DeviceStatusRequest(BaseModel):
    device_id: str = DEFAULT_DEVICE_ID
    sent_at: str | None = None
    online: bool
    temperature_c: float | None = None
    humidity_percent: float | None = None
    lux: float | None = None
    presence_detected: bool | None = None


class DeviceEventRequest(BaseModel):
    device_id: str = DEFAULT_DEVICE_ID
    event_at: str | None = None
    event_type: str
    todo_id: str | None = None
    temperature_c: float | None = None
    humidity_percent: float | None = None
    lux: float | None = None
    presence_detected: bool | None = None


class ModelChatRequest(BaseModel):
    message: str


def model_to_dict(model: BaseModel) -> dict[str, Any]:
    if hasattr(model, "model_dump"):
        return model.model_dump()
    return model.dict()


def now_iso() -> str:
    return datetime.now(timezone.utc).astimezone().isoformat(timespec="seconds")



def default_voice_settings() -> dict[str, bool]:
    return {
        "todo_voice_on": True,
        "alarm_voice_on": True,
        "env_voice_on": True,
        "env_alert_on": True,
        "home_hour_chime_on": True,
    }



def default_alarm_items() -> list[dict[str, Any]]:
    return [
        {"hour": 7, "minute": 30, "repeat": True, "enabled": True, "voice": True},
        {"hour": 8, "minute": 0, "repeat": True, "enabled": True, "voice": True},
        {"hour": 20, "minute": 15, "repeat": False, "enabled": True, "voice": False},
    ]



def default_device_config() -> dict[str, Any]:
    return {
        "config_version": 1,
        "updated_at": now_iso(),
        "alarms": default_alarm_items(),
        "voice_settings": default_voice_settings(),
    }



def default_device_status() -> dict[str, Any]:
    return {
        "device_id": DEFAULT_DEVICE_ID,
        "updated_at": "",
        "online": False,
        "temperature_c": None,
        "humidity_percent": None,
        "lux": None,
        "presence_detected": None,
    }



def ensure_storage() -> None:
    DATA_DIR.mkdir(parents=True, exist_ok=True)

    active_missing = not ACTIVE_TODOS_FILE.exists()
    completed_missing = not COMPLETED_TODOS_FILE.exists()
    if LEGACY_TODOS_FILE.exists() and active_missing and completed_missing:
        raw_items = read_json_file(LEGACY_TODOS_FILE, default=[])
        if not isinstance(raw_items, list):
            raise HTTPException(status_code=500, detail="legacy todos.json root must be a JSON array")
        active_items: list[dict[str, Any]] = []
        completed_items: list[dict[str, Any]] = []
        for item in raw_items:
            if not isinstance(item, dict):
                continue
            target = completed_items if bool(item.get("done")) else active_items
            normalized = {
                "id": str(item.get("id", "")),
                "text": str(item.get("text", "")),
                "done": bool(item.get("done", False)),
                "updated_at": str(item.get("updated_at", now_iso())),
            }
            if normalized["id"] and normalized["text"]:
                if normalized["done"]:
                    normalized["completed_at"] = normalized["updated_at"]
                target.append(normalized)
        write_json_file(ACTIVE_TODOS_FILE, active_items)
        write_json_file(COMPLETED_TODOS_FILE, completed_items)
    else:
        if active_missing:
            write_json_file(ACTIVE_TODOS_FILE, [])
        if completed_missing:
            write_json_file(COMPLETED_TODOS_FILE, [])

    if not DEVICE_STATUS_FILE.exists():
        write_json_file(DEVICE_STATUS_FILE, default_device_status())
    if not EVENT_HISTORY_FILE.exists():
        write_json_file(EVENT_HISTORY_FILE, [])
    if not DEVICE_CONFIG_FILE.exists():
        write_json_file(DEVICE_CONFIG_FILE, default_device_config())
    ensure_ai_storage(MODEL_DIALOG_MEMORY_FILE, MODEL_PRESENCE_RUNTIME_FILE)



def read_json_file(path: Path, default: Any) -> Any:
    if not path.exists():
        return default
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        raise HTTPException(status_code=500, detail=f"{path.name} is invalid JSON") from exc
    except OSError as exc:
        raise HTTPException(status_code=500, detail=f"Failed to read {path.name}") from exc



def write_json_file(path: Path, payload: Any) -> None:
    temp_file = path.with_suffix(path.suffix + ".tmp")
    body = json.dumps(payload, ensure_ascii=False, indent=2) + "\n"
    try:
        temp_file.write_text(body, encoding="utf-8")
        temp_file.replace(path)
    except OSError as exc:
        raise HTTPException(status_code=500, detail=f"Failed to write {path.name}") from exc



def read_active_todos() -> list[dict[str, Any]]:
    ensure_storage()
    items = read_json_file(ACTIVE_TODOS_FILE, [])
    if not isinstance(items, list):
        raise HTTPException(status_code=500, detail="todos_active.json root must be a JSON array")
    return items



def read_completed_todos() -> list[dict[str, Any]]:
    ensure_storage()
    items = read_json_file(COMPLETED_TODOS_FILE, [])
    if not isinstance(items, list):
        raise HTTPException(status_code=500, detail="todos_completed.json root must be a JSON array")
    return items



def read_device_config() -> dict[str, Any]:
    ensure_storage()
    raw = read_json_file(DEVICE_CONFIG_FILE, default_device_config())
    if not isinstance(raw, dict):
        raise HTTPException(status_code=500, detail="device_config.json root must be a JSON object")

    config = default_device_config()
    config["config_version"] = int(raw.get("config_version", config["config_version"]))
    config["updated_at"] = str(raw.get("updated_at", config["updated_at"]))
    config["alarms"] = normalize_alarm_items(raw.get("alarms", config["alarms"]))
    config["voice_settings"] = normalize_voice_settings(raw.get("voice_settings", config["voice_settings"]))
    return config



def write_device_config(config: dict[str, Any]) -> None:
    write_json_file(DEVICE_CONFIG_FILE, config)



def normalize_alarm_item(item: Any) -> dict[str, Any]:
    if not isinstance(item, dict):
        raise HTTPException(status_code=400, detail="alarm item must be an object")
    hour = int(item.get("hour", 0))
    minute = int(item.get("minute", 0))
    if hour < 0 or hour > 23:
        raise HTTPException(status_code=400, detail="alarm hour must be between 0 and 23")
    if minute < 0 or minute > 59:
        raise HTTPException(status_code=400, detail="alarm minute must be between 0 and 59")
    return {
        "hour": hour,
        "minute": minute,
        "repeat": bool(item.get("repeat", False)),
        "enabled": bool(item.get("enabled", False)),
        "voice": bool(item.get("voice", False)),
    }



def normalize_alarm_items(items: Any) -> list[dict[str, Any]]:
    if not isinstance(items, list):
        raise HTTPException(status_code=400, detail="alarms must be a JSON array")
    normalized = [normalize_alarm_item(item) for item in items]
    if len(normalized) > 5:
        raise HTTPException(status_code=400, detail="at most 5 alarms are supported")
    return normalized



def normalize_voice_settings(payload: Any) -> dict[str, bool]:
    if not isinstance(payload, dict):
        raise HTTPException(status_code=400, detail="voice_settings must be a JSON object")
    defaults = default_voice_settings()
    return {
        "todo_voice_on": bool(payload.get("todo_voice_on", defaults["todo_voice_on"])),
        "alarm_voice_on": bool(payload.get("alarm_voice_on", defaults["alarm_voice_on"])),
        "env_voice_on": bool(payload.get("env_voice_on", defaults["env_voice_on"])),
        "env_alert_on": bool(payload.get("env_alert_on", defaults["env_alert_on"])),
        "home_hour_chime_on": bool(payload.get("home_hour_chime_on", defaults["home_hour_chime_on"])),
    }



def bump_config_version() -> dict[str, Any]:
    config = read_device_config()
    config["config_version"] = int(config.get("config_version", 0)) + 1
    config["updated_at"] = now_iso()
    write_device_config(config)
    return config



def validate_text(value: str) -> str:
    text = value.strip()
    if not text:
        raise HTTPException(status_code=400, detail="text must not be empty")
    if len(text) > MAX_TEXT_LENGTH:
        raise HTTPException(status_code=400, detail=f"text must be at most {MAX_TEXT_LENGTH} characters")
    return text



def find_todo(items: list[dict[str, Any]], todo_id: str) -> dict[str, Any]:
    for item in items:
        if item.get("id") == todo_id:
            return item
    raise HTTPException(status_code=404, detail="Todo not found")



def find_todo_index(items: list[dict[str, Any]], todo_id: str) -> int:
    for index, item in enumerate(items):
        if item.get("id") == todo_id:
            return index
    raise HTTPException(status_code=404, detail="Todo not found")



def list_updated_at(items: list[dict[str, Any]]) -> str:
    if not items:
        return now_iso()
    return max(str(item.get("updated_at", "")) for item in items) or now_iso()



def active_todo_response(items: list[dict[str, Any]]) -> list[dict[str, Any]]:
    response: list[dict[str, Any]] = []
    for item in items:
        response.append(
            {
                "id": str(item.get("id", "")),
                "text": str(item.get("text", "")),
                "done": False,
                "updated_at": str(item.get("updated_at", "")),
            }
        )
    return response



def completed_todo_response(items: list[dict[str, Any]]) -> list[dict[str, Any]]:
    response: list[dict[str, Any]] = []
    for item in items:
        response.append(
            {
                "id": str(item.get("id", "")),
                "text": str(item.get("text", "")),
                "done": True,
                "updated_at": str(item.get("updated_at", "")),
                "completed_at": str(item.get("completed_at", item.get("updated_at", ""))),
            }
        )
    return response



def build_device_config_response() -> dict[str, Any]:
    config = read_device_config()
    active_items = read_active_todos()
    return {
        "config_version": config["config_version"],
        "updated_at": config["updated_at"],
        "todos_active": active_todo_response(active_items),
        "alarms": config["alarms"],
        "voice_settings": config["voice_settings"],
    }



def apply_config_version_guard(request_version: int | None, current_version: int) -> None:
    if request_version is None:
        return
    if request_version != current_version:
        raise HTTPException(status_code=409, detail="config version conflict")



def move_active_todo_to_completed(todo_id: str) -> dict[str, Any]:
    active_items = read_active_todos()
    completed_items = read_completed_todos()
    index = find_todo_index(active_items, todo_id)
    item = dict(active_items.pop(index))
    completed_at = now_iso()
    item["done"] = True
    item["updated_at"] = completed_at
    item["completed_at"] = completed_at
    completed_items.append(item)
    write_json_file(COMPLETED_TODOS_FILE, completed_items)
    write_json_file(ACTIVE_TODOS_FILE, active_items)
    bump_config_version()
    return {
        "id": item["id"],
        "text": item["text"],
        "done": True,
        "updated_at": item["updated_at"],
        "completed_at": item["completed_at"],
    }



def delete_active_todo(todo_id: str) -> None:
    active_items = read_active_todos()
    index = find_todo_index(active_items, todo_id)
    del active_items[index]
    write_json_file(ACTIVE_TODOS_FILE, active_items)
    bump_config_version()



def read_device_status() -> dict[str, Any]:
    ensure_storage()
    raw = read_json_file(DEVICE_STATUS_FILE, default_device_status())
    if not isinstance(raw, dict):
        raise HTTPException(status_code=500, detail="device_status.json root must be a JSON object")
    status = default_device_status()
    status.update(raw)
    return status



def read_event_history() -> list[dict[str, Any]]:
    ensure_storage()
    raw = read_json_file(EVENT_HISTORY_FILE, [])
    if not isinstance(raw, list):
        raise HTTPException(status_code=500, detail="event_history.json root must be a JSON array")
    return raw


def sync_presence_runtime_from_status(status: dict[str, Any]) -> dict[str, Any]:
    ensure_storage()
    runtime_state = read_presence_runtime(MODEL_PRESENCE_RUNTIME_FILE)
    next_state = advance_presence_runtime(runtime_state, status)
    if next_state != runtime_state:
        write_presence_runtime(MODEL_PRESENCE_RUNTIME_FILE, next_state)
    return next_state.model_dump()


def get_presence_runtime_for_status(status: dict[str, Any]) -> Any:
    runtime_state = read_presence_runtime(MODEL_PRESENCE_RUNTIME_FILE)
    next_state = advance_presence_runtime(runtime_state, status)
    if next_state != runtime_state:
        write_presence_runtime(MODEL_PRESENCE_RUNTIME_FILE, next_state)
    return next_state


def get_chat_context_data() -> dict[str, Any]:
    status = read_device_status()
    config = read_device_config()
    open_todos = read_active_todos()
    dialog_memory_items = read_dialog_memory(MODEL_DIALOG_MEMORY_FILE)
    runtime_state = get_presence_runtime_for_status(status)
    return build_chat_context(status, config, open_todos, dialog_memory_items, runtime_state)


def get_report_context_data() -> dict[str, Any]:
    status = read_device_status()
    config = read_device_config()
    open_todos = read_active_todos()
    event_history = read_event_history()
    runtime_state = get_presence_runtime_for_status(status)
    return build_report_context(status, config, open_todos, event_history, runtime_state)


def report_record_to_response_payload(report_record: Any, include_input_snapshot: bool = False) -> dict[str, Any]:
    payload = {
        "generated_at": report_record.generated_at,
        "render_version": report_record.render_version,
        "report_fields": report_record.report_fields,
    }
    if include_input_snapshot:
        payload["input_snapshot"] = report_record.input_snapshot
    return payload



def update_device_config(alarms: list[dict[str, Any]] | None, voice_settings: dict[str, bool] | None, config_version: int | None) -> dict[str, Any]:
    config = read_device_config()
    apply_config_version_guard(config_version, config["config_version"])

    changed = False
    if alarms is not None and alarms != config["alarms"]:
        config["alarms"] = alarms
        changed = True
    if voice_settings is not None and voice_settings != config["voice_settings"]:
        config["voice_settings"] = voice_settings
        changed = True

    if changed:
        config["config_version"] += 1
        config["updated_at"] = now_iso()
        write_device_config(config)

    return {
        "config_version": config["config_version"],
        "updated_at": config["updated_at"],
        "alarms": config["alarms"],
        "voice_settings": config["voice_settings"],
    }


def render_page(request: Request, name: str, active_page: str, page_title: str) -> HTMLResponse:
    return templates.TemplateResponse(
        request=request,
        name=name,
        context={
            "active_page": active_page,
            "page_title": page_title,
        },
    )


@app.get("/", response_class=HTMLResponse)
def index(request: Request) -> RedirectResponse:
    return RedirectResponse(url="/status", status_code=302)


@app.get("/status", response_class=HTMLResponse)
def status_page(request: Request) -> HTMLResponse:
    return render_page(request, "status.html", "status", "设备状态")


@app.get("/todos", response_class=HTMLResponse)
def todos_page(request: Request) -> HTMLResponse:
    return render_page(request, "todos.html", "todos", "待办事项")


@app.get("/alarms", response_class=HTMLResponse)
def alarms_page(request: Request) -> HTMLResponse:
    return render_page(request, "alarms.html", "alarms", "闹钟管理")


@app.get("/voice", response_class=HTMLResponse)
def voice_page(request: Request) -> HTMLResponse:
    return render_page(request, "voice.html", "voice", "语音设置")


@app.get("/events", response_class=HTMLResponse)
def events_page(request: Request) -> HTMLResponse:
    return render_page(request, "events.html", "events", "事件历史")


@app.get("/model", response_class=HTMLResponse)
def model_page(request: Request) -> HTMLResponse:
    return render_page(request, "model.html", "model", "模型对话页")


@app.get("/health")
def health() -> dict[str, str]:
    ensure_storage()
    return {"status": "ok"}


@app.get("/api/todos")
def get_todos() -> dict[str, Any]:
    active_items = read_active_todos()
    completed_items = read_completed_todos()
    return {
        "items": active_todo_response(active_items),
        "completed_items": completed_todo_response(completed_items),
        "updated_at": list_updated_at(active_items),
    }


@app.post("/api/todos", status_code=201)
def create_todo(payload: CreateTodoRequest) -> dict[str, Any]:
    items = read_active_todos()
    item = {
        "id": f"todo-{uuid.uuid4().hex[:8]}",
        "text": validate_text(payload.text),
        "done": False,
        "updated_at": now_iso(),
    }
    items.append(item)
    write_json_file(ACTIVE_TODOS_FILE, items)
    bump_config_version()
    return {
        "id": item["id"],
        "text": item["text"],
        "done": False,
        "updated_at": item["updated_at"],
    }


@app.put("/api/todos/reorder")
def reorder_todos(payload: ReorderTodosRequest) -> dict[str, Any]:
    items = read_active_todos()
    current_ids = [str(item.get("id")) for item in items]
    if len(payload.ids) != len(current_ids):
        raise HTTPException(status_code=400, detail="ids must include all active todos")
    if set(payload.ids) != set(current_ids):
        raise HTTPException(status_code=400, detail="ids must exactly match current active todos")

    item_map = {str(item["id"]): item for item in items}
    timestamp = now_iso()
    reordered = []
    for todo_id in payload.ids:
        item = dict(item_map[todo_id])
        item["updated_at"] = timestamp
        reordered.append(item)

    write_json_file(ACTIVE_TODOS_FILE, reordered)
    config = bump_config_version()
    return {"ok": True, "updated_at": timestamp, "config_version": config["config_version"]}


@app.put("/api/todos/{todo_id}")
def update_todo(todo_id: str, payload: UpdateTodoRequest) -> dict[str, Any]:
    if payload.text is None and payload.done is None:
        raise HTTPException(status_code=400, detail="At least one field must be provided")

    active_items = read_active_todos()
    item = find_todo(active_items, todo_id)

    if payload.text is not None:
        item["text"] = validate_text(payload.text)
        item["updated_at"] = now_iso()
        write_json_file(ACTIVE_TODOS_FILE, active_items)
        bump_config_version()

    if payload.done is True:
        return move_active_todo_to_completed(todo_id)
    if payload.done is False:
        return {
            "id": str(item.get("id", "")),
            "text": str(item.get("text", "")),
            "done": False,
            "updated_at": str(item.get("updated_at", "")),
        }

    return {
        "id": str(item.get("id", "")),
        "text": str(item.get("text", "")),
        "done": False,
        "updated_at": str(item.get("updated_at", "")),
    }


@app.post("/api/todos/{todo_id}/complete")
def complete_todo(todo_id: str) -> dict[str, Any]:
    return move_active_todo_to_completed(todo_id)


@app.delete("/api/todos/{todo_id}", status_code=204)
def delete_todo(todo_id: str) -> None:
    delete_active_todo(todo_id)


@app.get("/api/device/config")
def get_device_config() -> dict[str, Any]:
    return build_device_config_response()


@app.put("/api/device/config")
def put_device_config(payload: DeviceConfigUpdateRequest) -> dict[str, Any]:
    alarms = normalize_alarm_items([model_to_dict(item) for item in payload.alarms]) if payload.alarms is not None else None
    voice_settings = normalize_voice_settings(model_to_dict(payload.voice_settings)) if payload.voice_settings is not None else None
    return update_device_config(alarms, voice_settings, payload.config_version)


@app.put("/api/device/alarms")
def put_device_alarms(payload: DeviceAlarmsRequest) -> dict[str, Any]:
    alarms = normalize_alarm_items([model_to_dict(item) for item in payload.alarms])
    return update_device_config(alarms, None, payload.config_version)


@app.put("/api/device/voice-settings")
def put_device_voice_settings(payload: DeviceVoiceSettingsRequest) -> dict[str, Any]:
    voice_settings = normalize_voice_settings(model_to_dict(payload.voice_settings))
    return update_device_config(None, voice_settings, payload.config_version)


@app.post("/api/device/status")
def post_device_status(payload: DeviceStatusRequest) -> dict[str, Any]:
    status = {
        "device_id": payload.device_id,
        "updated_at": payload.sent_at or now_iso(),
        "online": payload.online,
        "temperature_c": payload.temperature_c,
        "humidity_percent": payload.humidity_percent,
        "lux": payload.lux,
        "presence_detected": payload.presence_detected,
    }
    write_json_file(DEVICE_STATUS_FILE, status)
    sync_presence_runtime_from_status(status)
    return {"ok": True, "updated_at": status["updated_at"]}


@app.get("/api/device/status")
def get_device_status() -> dict[str, Any]:
    return read_device_status()


@app.post("/api/device/events")
def post_device_event(payload: DeviceEventRequest) -> dict[str, Any]:
    events = read_event_history()
    event_type = payload.event_type.strip()
    if not event_type:
        raise HTTPException(status_code=400, detail="event_type must not be empty")
    event = {
        "device_id": payload.device_id,
        "event_at": payload.event_at or now_iso(),
        "event_type": event_type,
        "todo_id": payload.todo_id,
        "temperature_c": payload.temperature_c,
        "humidity_percent": payload.humidity_percent,
        "lux": payload.lux,
        "presence_detected": payload.presence_detected,
    }
    events.append(event)
    if len(events) > MAX_EVENT_HISTORY:
        events = events[-MAX_EVENT_HISTORY:]
    write_json_file(EVENT_HISTORY_FILE, events)
    return {"ok": True, "event_at": event["event_at"]}


@app.get("/api/device/events")
def get_device_events(
    event_type: str | None = Query(default=None),
    date_from: str | None = Query(default=None),
    date_to: str | None = Query(default=None),
    limit: int = Query(default=50, ge=1, le=200),
) -> dict[str, Any]:
    events = read_event_history()

    filtered = []
    for event in events:
        current_type = str(event.get("event_type", ""))
        current_time = str(event.get("event_at", ""))
        if event_type and current_type != event_type:
            continue
        if date_from and current_time < date_from:
            continue
        if date_to and current_time > date_to:
            continue
        filtered.append(event)

    filtered = filtered[-limit:]
    filtered.reverse()
    return {"items": filtered, "count": len(filtered)}


@app.post("/api/model/chat")
async def post_model_chat(payload: ModelChatRequest) -> dict[str, Any]:
    try:
        settings = load_ai_settings_from_env()
        context = get_chat_context_data()
        result = await generate_chat_response(settings, context, payload.message)
        append_dialog_memory(MODEL_DIALOG_MEMORY_FILE, result.memory_item)
        return {"answer_text": result.answer_text}
    except AIServiceError as exc:
        raise HTTPException(status_code=exc.status_code, detail=exc.detail) from exc


@app.delete("/api/model/memory")
def delete_model_memory() -> dict[str, bool]:
    clear_dialog_memory(MODEL_DIALOG_MEMORY_FILE)
    return {"ok": True}


@app.post("/api/model/report/generate")
async def post_model_report_generate() -> dict[str, Any]:
    try:
        settings = load_ai_settings_from_env()
        context = get_report_context_data()
        report_record = await generate_health_report(settings, context)
        write_latest_report(LATEST_HEALTH_REPORT_FILE, report_record)
        return report_record_to_response_payload(report_record)
    except AIServiceError as exc:
        raise HTTPException(status_code=exc.status_code, detail=exc.detail) from exc


@app.get("/api/model/report")
def get_model_report() -> dict[str, Any]:
    report_record = read_latest_report(LATEST_HEALTH_REPORT_FILE)
    if report_record is None:
        raise HTTPException(status_code=404, detail="health report not found")
    return report_record_to_response_payload(report_record)


@app.get("/api/model/report/pdf")
def get_model_report_pdf() -> Response:
    report_record = read_latest_report(LATEST_HEALTH_REPORT_FILE)
    if report_record is None:
        raise HTTPException(status_code=404, detail="health report not found")

    try:
        pdf_bytes = build_health_report_pdf(report_record.model_dump())
    except ValueError as exc:
        raise HTTPException(status_code=500, detail=str(exc)) from exc
    except Exception as exc:
        raise HTTPException(status_code=500, detail="failed to build health report pdf") from exc

    return Response(
        content=pdf_bytes,
        media_type="application/pdf",
        headers={
            "Content-Disposition": 'attachment; filename="smart-clock-health-report.pdf"',
        },
    )
