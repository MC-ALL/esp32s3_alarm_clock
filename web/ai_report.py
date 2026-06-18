from __future__ import annotations

import json
from datetime import datetime, timezone
from typing import Literal

from pydantic import BaseModel

from ai_client import AIServiceError, AISettings, call_openai_compatible_json
from ai_storage import HealthReportRecord

REPORT_RENDER_VERSION = "v1"

REPORT_SYSTEM_PROMPT = """
你是 Smart Clock Web 控制中心中的办公健康分析助手。

任务边界：
1. 你只能基于输入中的当前设备状态、连续在位时长、Todo 摘要和设备事件摘要进行分析。
2. 你不能伪造不存在的数据，也不能夸大风险。
3. 输出内容必须专业、严谨、克制，不说废话，不写泛泛而谈的空句。
4. 不要输出最终 HTML 或 PDF，只输出结构化 JSON。

返回 JSON 结构：
{
  "report_title": "报告标题",
  "risk_level": "low | medium | high",
  "overall_summary": "总体结论",
  "environment_analysis": "环境状态分析",
  "sedentary_analysis": "连续在位/久坐分析",
  "todo_and_routine_advice": "待办与作息建议",
  "improvement_actions": ["建议 1", "建议 2"],
  "key_findings": ["发现 1", "发现 2"]
}
""".strip()


class ReportAIResponse(BaseModel):
    report_title: str
    risk_level: Literal["low", "medium", "high"]
    overall_summary: str
    environment_analysis: str
    sedentary_analysis: str
    todo_and_routine_advice: str
    improvement_actions: list[str]
    key_findings: list[str]


def _now_iso() -> str:
    return datetime.now(timezone.utc).astimezone().isoformat(timespec="seconds")


def _normalize_required_text(value: str, field_name: str) -> str:
    text = value.strip()
    if not text:
        raise AIServiceError(f"ai report response field {field_name} is empty", status_code=502)
    return text


def _normalize_text_list(values: list[str], field_name: str, limit: int = 5) -> list[str]:
    normalized: list[str] = []
    for value in values:
        item = value.strip()
        if item:
            normalized.append(item)
        if len(normalized) >= limit:
            break
    if not normalized:
        raise AIServiceError(f"ai report response field {field_name} is empty", status_code=502)
    return normalized


async def generate_health_report(settings: AISettings, context: dict[str, object]) -> HealthReportRecord:
    payload = {
        "report_context": context,
    }
    messages = [
        {"role": "system", "content": REPORT_SYSTEM_PROMPT},
        {
            "role": "user",
            "content": json.dumps(payload, ensure_ascii=False, indent=2),
        },
    ]
    response = await call_openai_compatible_json(
        settings=settings,
        messages=messages,
        response_schema=ReportAIResponse,
        temperature=0.2,
    )

    report_fields = {
        "report_title": _normalize_required_text(response.report_title, "report_title"),
        "risk_level": response.risk_level,
        "overall_summary": _normalize_required_text(response.overall_summary, "overall_summary"),
        "environment_analysis": _normalize_required_text(response.environment_analysis, "environment_analysis"),
        "sedentary_analysis": _normalize_required_text(response.sedentary_analysis, "sedentary_analysis"),
        "todo_and_routine_advice": _normalize_required_text(
            response.todo_and_routine_advice,
            "todo_and_routine_advice",
        ),
        "improvement_actions": _normalize_text_list(response.improvement_actions, "improvement_actions"),
        "key_findings": _normalize_text_list(response.key_findings, "key_findings"),
    }

    return HealthReportRecord(
        generated_at=_now_iso(),
        render_version=REPORT_RENDER_VERSION,
        input_snapshot=context,
        report_fields=report_fields,
    )
