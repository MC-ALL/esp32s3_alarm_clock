from __future__ import annotations

import json
import os
import uuid
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from fastapi import FastAPI, HTTPException
from fastapi.responses import HTMLResponse
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel
from starlette.templating import Jinja2Templates
from starlette.requests import Request

BASE_DIR = Path(__file__).resolve().parent
DATA_DIR = Path(os.getenv("CLOCK_WEB_DATA_DIR", str(BASE_DIR / "data")))
DATA_FILE = DATA_DIR / "todos.json"
MAX_TEXT_LENGTH = 96

app = FastAPI(title="Clock Todo Web")
app.mount("/static", StaticFiles(directory=BASE_DIR / "static"), name="static")
templates = Jinja2Templates(directory=str(BASE_DIR / "templates"))


class CreateTodoRequest(BaseModel):
    text: str


class UpdateTodoRequest(BaseModel):
    text: str | None = None
    done: bool | None = None


class ReorderTodosRequest(BaseModel):
    ids: list[str]



def now_iso() -> str:
    return datetime.now(timezone.utc).astimezone().isoformat(timespec="seconds")



def ensure_storage() -> None:
    DATA_DIR.mkdir(parents=True, exist_ok=True)
    if not DATA_FILE.exists():
        DATA_FILE.write_text("[]\n", encoding="utf-8")



def read_todos() -> list[dict[str, Any]]:
    ensure_storage()
    try:
        raw = json.loads(DATA_FILE.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        raise HTTPException(status_code=500, detail="todos.json is invalid JSON") from exc
    except OSError as exc:
        raise HTTPException(status_code=500, detail="Failed to read todos.json") from exc

    if not isinstance(raw, list):
        raise HTTPException(status_code=500, detail="todos.json root must be a JSON array")

    return raw



def write_todos(items: list[dict[str, Any]]) -> None:
    ensure_storage()
    temp_file = DATA_FILE.with_suffix(".tmp")
    payload = json.dumps(items, ensure_ascii=False, indent=2) + "\n"
    try:
        temp_file.write_text(payload, encoding="utf-8")
        temp_file.replace(DATA_FILE)
    except OSError as exc:
        raise HTTPException(status_code=500, detail="Failed to write todos.json") from exc



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



def list_updated_at(items: list[dict[str, Any]]) -> str:
    if not items:
        return now_iso()
    return max(item.get("updated_at", "") for item in items) or now_iso()


@app.get("/", response_class=HTMLResponse)
def index(request: Request) -> HTMLResponse:
    return templates.TemplateResponse("index.html", {"request": request})


@app.get("/health")
def health() -> dict[str, str]:
    ensure_storage()
    return {"status": "ok"}


@app.get("/api/todos")
def get_todos() -> dict[str, Any]:
    items = read_todos()
    return {"items": items, "updated_at": list_updated_at(items)}


@app.post("/api/todos", status_code=201)
def create_todo(payload: CreateTodoRequest) -> dict[str, Any]:
    items = read_todos()
    item = {
        "id": f"todo-{uuid.uuid4().hex[:8]}",
        "text": validate_text(payload.text),
        "done": False,
        "updated_at": now_iso(),
    }
    items.append(item)
    write_todos(items)
    return item


@app.put("/api/todos/reorder")
def reorder_todos(payload: ReorderTodosRequest) -> dict[str, Any]:
    items = read_todos()
    current_ids = [item.get("id") for item in items]

    if len(payload.ids) != len(current_ids):
        raise HTTPException(status_code=400, detail="ids must include all todos")
    if set(payload.ids) != set(current_ids):
        raise HTTPException(status_code=400, detail="ids must exactly match current todos")

    item_map = {item["id"]: item for item in items}
    reordered = [item_map[todo_id] for todo_id in payload.ids]
    timestamp = now_iso()
    for item in reordered:
        item["updated_at"] = timestamp

    write_todos(reordered)
    return {"ok": True, "updated_at": timestamp}


@app.put("/api/todos/{todo_id}")
def update_todo(todo_id: str, payload: UpdateTodoRequest) -> dict[str, Any]:
    if payload.text is None and payload.done is None:
        raise HTTPException(status_code=400, detail="At least one field must be provided")

    items = read_todos()
    item = find_todo(items, todo_id)

    if payload.text is not None:
        item["text"] = validate_text(payload.text)
    if payload.done is not None:
        item["done"] = payload.done

    item["updated_at"] = now_iso()
    write_todos(items)
    return item


@app.delete("/api/todos/{todo_id}", status_code=204)
def delete_todo(todo_id: str) -> None:
    items = read_todos()
    find_todo(items, todo_id)
    remaining = [item for item in items if item.get("id") != todo_id]
    write_todos(remaining)
