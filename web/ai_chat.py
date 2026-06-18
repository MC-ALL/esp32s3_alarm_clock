from __future__ import annotations

import json
import uuid
from datetime import datetime, timezone

from pydantic import BaseModel

from ai_client import AIServiceError, AISettings, call_openai_compatible_json
from ai_storage import DialogMemoryItem

MAX_MODEL_MESSAGE_LENGTH = 400

CHAT_SYSTEM_PROMPT = """
你是 Smart Clock Web 控制中心中的垂直只读助手。

职责边界：
1. 只围绕当前环境状态、连续在位时长、未完成 Todo、工作节奏和办公健康建议展开。
2. 你不能修改系统配置，也不能假装执行任何写操作。
3. 如果用户问题明显脱离设备环境、作息建议或 Todo 主题，请简短说明边界，并把话题拉回当前设备场景。
4. 不要伪造不存在的数据；如果输入里缺失某项，就坦率说明。

表达要求：
1. 对用户的回答要简洁、自然、略带风趣，但不能轻浮。
2. 不要写成长文，不要堆砌免责声明。
3. 只输出合法 JSON，不要输出 Markdown，不要输出额外解释。

返回 JSON 结构：
{
  "answer_text": "给用户看的简洁回答",
  "memory_summary": "不给用户看，用于后续上下文记忆的中性概括，应压缩本轮问题、关键数据和结论"
}
""".strip()


class ChatAIResponse(BaseModel):
    answer_text: str
    memory_summary: str


class ChatServiceResult(BaseModel):
    answer_text: str
    memory_item: DialogMemoryItem


def _now_iso() -> str:
    return datetime.now(timezone.utc).astimezone().isoformat(timespec="seconds")


def _clip_text(value: str, limit: int) -> str:
    text = value.strip()
    if len(text) <= limit:
        return text
    return text[: max(0, limit - 1)].rstrip() + "…"


def _normalize_required_text(value: str, field_name: str) -> str:
    text = value.strip()
    if not text:
        raise AIServiceError(f"ai chat response field {field_name} is empty", status_code=502)
    return text


async def generate_chat_response(settings: AISettings, context: dict[str, object], user_message: str) -> ChatServiceResult:
    message = user_message.strip()
    if not message:
        raise AIServiceError("message must not be empty", status_code=400)
    if len(message) > MAX_MODEL_MESSAGE_LENGTH:
        raise AIServiceError(
            f"message must be at most {MAX_MODEL_MESSAGE_LENGTH} characters",
            status_code=400,
        )

    payload = {
        "current_context": context,
        "user_message": message,
    }
    messages = [
        {"role": "system", "content": CHAT_SYSTEM_PROMPT},
        {
            "role": "user",
            "content": json.dumps(payload, ensure_ascii=False, indent=2),
        },
    ]
    response = await call_openai_compatible_json(
        settings=settings,
        messages=messages,
        response_schema=ChatAIResponse,
        temperature=0.2,
    )

    answer_text = _normalize_required_text(response.answer_text, "answer_text")
    memory_summary = _normalize_required_text(response.memory_summary, "memory_summary")
    memory_item = DialogMemoryItem(
        id="mem-%s" % uuid.uuid4().hex[:8],
        created_at=_now_iso(),
        summary_text=memory_summary,
        source_user_input_excerpt=_clip_text(message, 96),
        source_answer_excerpt=_clip_text(answer_text, 144),
    )
    return ChatServiceResult(answer_text=answer_text, memory_item=memory_item)
