from __future__ import annotations

import asyncio
import json
import os
from pathlib import Path
from typing import Any, Optional, Type, TypeVar
from urllib import error as urllib_error
from urllib import request as urllib_request

from pydantic import BaseModel, ValidationError

DEFAULT_AI_PROVIDER = "openai_compatible"
DEFAULT_AI_BASE_URL = "https://api.deepseek.com/v1"
DEFAULT_AI_MODEL_VARIANT = "reasoner"
DEFAULT_AI_API_KEY_FILE = "/runtime/secrets/backend_ai_api_key.txt"
DEFAULT_AI_REQUEST_TIMEOUT_S = 60.0
DEFAULT_AI_MAX_TOKENS = 4096

AI_MODEL_VARIANT_MAPPING = {
    "chat": "deepseek-chat",
    "reasoner": "deepseek-reasoner",
}

T = TypeVar("T", bound=BaseModel)


class AIServiceError(RuntimeError):
    def __init__(self, detail: str, status_code: int = 503):
        super().__init__(detail)
        self.detail = detail
        self.status_code = status_code


class AISettings(BaseModel):
    provider: str = DEFAULT_AI_PROVIDER
    base_url: str = DEFAULT_AI_BASE_URL
    model_variant: str = DEFAULT_AI_MODEL_VARIANT
    model: str
    api_key: str
    request_timeout_s: float = DEFAULT_AI_REQUEST_TIMEOUT_S
    max_tokens: int = DEFAULT_AI_MAX_TOKENS


def resolve_api_key(api_key: Optional[str], api_key_file: Optional[str]) -> Optional[str]:
    if isinstance(api_key, str) and api_key.strip():
        return api_key.strip()

    candidate = api_key_file or DEFAULT_AI_API_KEY_FILE
    path = Path(candidate)
    if not path.exists():
        return None

    secret = path.read_text(encoding="utf-8").strip()
    return secret or None


def resolve_model(model: Optional[str], model_variant: Optional[str]) -> str:
    if isinstance(model, str) and model.strip():
        return model.strip()

    variant = model_variant.strip() if isinstance(model_variant, str) and model_variant.strip() else DEFAULT_AI_MODEL_VARIANT
    resolved = AI_MODEL_VARIANT_MAPPING.get(variant)
    if resolved is None:
        raise AIServiceError(
            "invalid ai model_variant: %s (expected one of: %s)"
            % (variant, ", ".join(sorted(AI_MODEL_VARIANT_MAPPING))),
            status_code=500,
        )
    return resolved


def load_ai_settings_from_env() -> AISettings:
    provider = os.getenv("BACKEND_AI_PROVIDER", DEFAULT_AI_PROVIDER).strip() or DEFAULT_AI_PROVIDER
    if provider != DEFAULT_AI_PROVIDER:
        raise AIServiceError("unsupported ai provider: %s" % provider, status_code=500)

    base_url = os.getenv("BACKEND_AI_BASE_URL", DEFAULT_AI_BASE_URL).strip() or DEFAULT_AI_BASE_URL
    model_variant = os.getenv("BACKEND_AI_MODEL_VARIANT", DEFAULT_AI_MODEL_VARIANT)
    model_override = os.getenv("BACKEND_AI_MODEL")
    api_key_override = os.getenv("BACKEND_AI_API_KEY")
    api_key_file = os.getenv("BACKEND_AI_API_KEY_FILE")

    try:
        request_timeout_s = float(os.getenv("BACKEND_AI_REQUEST_TIMEOUT_S", str(DEFAULT_AI_REQUEST_TIMEOUT_S)))
    except ValueError as exc:
        raise AIServiceError("invalid BACKEND_AI_REQUEST_TIMEOUT_S", status_code=500) from exc

    try:
        max_tokens = int(os.getenv("BACKEND_AI_MAX_TOKENS", str(DEFAULT_AI_MAX_TOKENS)))
    except ValueError as exc:
        raise AIServiceError("invalid BACKEND_AI_MAX_TOKENS", status_code=500) from exc

    model = resolve_model(model_override, model_variant)
    api_key = resolve_api_key(api_key_override, api_key_file)

    if not api_key:
        raise AIServiceError("ai api_key is required when provider=openai_compatible", status_code=503)
    if not base_url:
        raise AIServiceError("ai base_url is required when provider=openai_compatible", status_code=503)
    if not model:
        raise AIServiceError("ai model is required when provider=openai_compatible", status_code=503)

    return AISettings(
        provider=provider,
        base_url=base_url,
        model_variant=model_variant or DEFAULT_AI_MODEL_VARIANT,
        model=model,
        api_key=api_key,
        request_timeout_s=request_timeout_s,
        max_tokens=max_tokens,
    )


def _sync_call_openai_compatible_json(
    *,
    base_url: str,
    api_key: str,
    model: str,
    messages: list[dict[str, str]],
    max_tokens: int,
    timeout_s: float,
    temperature: Optional[float] = None,
) -> dict[str, Any]:
    if not api_key:
        raise AIServiceError("ai api_key is required when provider=openai_compatible", status_code=503)
    if not base_url:
        raise AIServiceError("ai base_url is required when provider=openai_compatible", status_code=503)
    if not model:
        raise AIServiceError("ai model is required when provider=openai_compatible", status_code=503)

    normalized_base_url = base_url.rstrip("/")
    request_url = (
        f"{normalized_base_url}/chat/completions"
        if normalized_base_url.endswith("/v1")
        else f"{normalized_base_url}/v1/chat/completions"
    )

    request_body: dict[str, Any] = {
        "model": model.strip(),
        "stream": False,
        "response_format": {"type": "json_object"},
        "max_tokens": max_tokens,
        "messages": messages,
    }

    if model.strip() != "deepseek-reasoner" and temperature is not None:
        request_body["temperature"] = temperature

    request = urllib_request.Request(
        request_url,
        method="POST",
        data=json.dumps(request_body, ensure_ascii=False).encode("utf-8"),
        headers={
            "Authorization": f"Bearer {api_key}",
            "Content-Type": "application/json",
        },
    )

    try:
        with urllib_request.urlopen(request, timeout=timeout_s) as response:
            raw_response = json.loads(response.read().decode("utf-8"))
    except urllib_error.HTTPError as exc:
        detail = exc.read().decode("utf-8", errors="replace").strip() or "ai provider request failed"
        raise AIServiceError(detail, status_code=502) from exc
    except urllib_error.URLError as exc:
        raise AIServiceError(f"ai provider request failed: {exc.reason}", status_code=502) from exc

    choices = raw_response.get("choices") or []
    if not choices:
        raise AIServiceError("ai provider returned empty choices", status_code=502)

    choice = choices[0]
    if choice.get("finish_reason") == "length":
        raise AIServiceError("ai provider response was truncated", status_code=502)

    message = choice.get("message") or {}
    content = message.get("content")
    if not isinstance(content, str) or not content.strip():
        raise AIServiceError("ai provider returned empty content", status_code=502)

    try:
        return json.loads(content)
    except json.JSONDecodeError as exc:
        raise AIServiceError("ai provider returned invalid json content", status_code=502) from exc


async def call_openai_compatible_json(
    *,
    settings: AISettings,
    messages: list[dict[str, str]],
    response_schema: Optional[Type[T]] = None,
    temperature: Optional[float] = 0.2,
) -> Any:
    parsed = await asyncio.to_thread(
        _sync_call_openai_compatible_json,
        base_url=settings.base_url,
        api_key=settings.api_key,
        model=settings.model,
        messages=messages,
        max_tokens=settings.max_tokens,
        timeout_s=settings.request_timeout_s,
        temperature=temperature,
    )

    if response_schema is None:
        return parsed

    try:
        return response_schema.model_validate(parsed)
    except ValidationError as exc:
        raise AIServiceError("ai provider response failed schema validation", status_code=502) from exc
