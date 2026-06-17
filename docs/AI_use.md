# AI 调用说明

本文档仅说明远端 AI 的通用接入方式，不包含任何具体业务逻辑。
本文档是当前项目调用远端 AI 的完整实现约定。若仅需保留 AI 调用能力，不需要再参考 `backend` 目录中的旧实现。

## 1. 基本约定

当前项目的远端 AI 接入采用 OpenAI 兼容模式：

- `provider = openai_compatible`
- `base_url = https://api.deepseek.com/v1`
- `model_variant = reasoner`
- `api_key` 通过文件读取
- 默认密钥文件路径：`/runtime/secrets/backend_ai_api_key.txt`
- 默认 `request_timeout_s = 60`
- 默认 `max_tokens = 4096`
- 当前层不做显式重试

模型变体映射如下：

- `chat -> deepseek-chat`
- `reasoner -> deepseek-reasoner`

当模型为 `deepseek-reasoner` 时，不传 `temperature`；其他模型默认可传 `temperature = 0.2`。

## 2. 配置解析规则

### 2.1 配置优先级

推荐使用以下优先级：

1. 显式函数入参
2. 配置文件
3. 密钥文件
4. 默认值

### 2.2 API Key 读取规则

API Key 固定采用文件接入方式，规则如下：

1. 若显式传入 `api_key`，优先使用
2. 否则读取 `api_key_file`
3. 若 `api_key_file` 未设置，默认读取 `/runtime/secrets/backend_ai_api_key.txt`

推荐实现：

```python
from pathlib import Path


def resolve_api_key(api_key: str | None, api_key_file: str | None) -> str | None:
    if isinstance(api_key, str) and api_key.strip():
        return api_key.strip()

    candidate = api_key_file or "/runtime/secrets/backend_ai_api_key.txt"
    path = Path(candidate)
    if not path.exists():
        return None

    secret = path.read_text(encoding="utf-8").strip()
    return secret or None
```

### 2.3 模型解析规则

推荐按以下规则得到最终模型名：

1. 若显式传入 `model`，使用 `model.strip()`
2. 若 `model` 为空，则按 `model_variant` 映射
3. `model_variant` 默认值为 `reasoner`
4. 非法 `model_variant` 直接抛异常

推荐实现：

```python
AI_MODEL_VARIANT_MAPPING = {
    "chat": "deepseek-chat",
    "reasoner": "deepseek-reasoner",
}


def resolve_model(model: str | None, model_variant: str | None) -> str:
    if isinstance(model, str) and model.strip():
        return model.strip()

    variant = model_variant.strip() if isinstance(model_variant, str) and model_variant.strip() else "reasoner"
    resolved = AI_MODEL_VARIANT_MAPPING.get(variant)
    if resolved is None:
        raise RuntimeError(
            f"invalid ai model_variant: {variant} (expected one of: {', '.join(sorted(AI_MODEL_VARIANT_MAPPING))})"
        )
    return resolved
```

### 2.4 环境变量命名

如果当前项目后续需要环境变量覆盖，推荐统一使用以下命名：

- `BACKEND_AI_PROVIDER`
- `BACKEND_AI_BASE_URL`
- `BACKEND_AI_MODEL_VARIANT`
- `BACKEND_AI_MODEL`
- `BACKEND_AI_API_KEY`
- `BACKEND_AI_API_KEY_FILE`
- `BACKEND_AI_REQUEST_TIMEOUT_S`
- `BACKEND_AI_MAX_TOKENS`

## 3. 接入方式

建议封装为通用函数：

```python
async def call_openai_compatible_json(
    *,
    base_url: str,
    api_key: str,
    model: str,
    model_variant: str | None = None,
    messages: list[dict[str, str]],
    max_tokens: int,
    timeout_s: float,
    response_schema: type | None = None,
    temperature: float | None = None,
) -> dict:
    ...
```

其中：

- `base_url`、`api_key`、`model`、`messages`、`max_tokens`、`timeout_s` 为必填
- `model_variant`、`response_schema`、`temperature` 为可选

建议拆分为两层：

- `_sync_call_openai_compatible_json(...)`：负责协议层
- `async call_openai_compatible_json(...)`：负责异步适配

## 4. 请求规则

### 4.1 URL

先对 `base_url` 做 `rstrip("/")`，再按以下规则拼接：

- 若 `base_url` 以 `/v1` 结尾，请求地址为 `base_url + "/chat/completions"`
- 否则，请求地址为 `base_url + "/v1/chat/completions"`

### 4.2 请求头

```json
{
  "Authorization": "Bearer <api_key>",
  "Content-Type": "application/json"
}
```

### 4.3 请求体

```json
{
  "model": "模型名",
  "stream": false,
  "response_format": {"type": "json_object"},
  "max_tokens": 4096,
  "messages": []
}
```

推荐保持：

- 非流式返回
- `response_format` 固定为 `json_object`
- 同步 HTTP 请求在异步环境中通过 `asyncio.to_thread(...)` 执行

### 4.4 温度策略

推荐按以下规则处理：

1. 若模型为 `deepseek-reasoner`，不传 `temperature`
2. 否则默认传 `temperature = 0.2`

示例：

```python
is_reasoner_model = model_name == "deepseek-reasoner"
if not is_reasoner_model and temperature is not None:
    request_body["temperature"] = temperature
```

## 5. messages 结构

`messages` 使用标准 Chat Completions 格式，通常至少包含两条消息：

```python
[
    {
        "role": "system",
        "content": "只输出合法 JSON，不要输出解释，不要输出 Markdown。"
    },
    {
        "role": "user",
        "content": "请根据输入生成结构化 JSON。"
    }
]
```

## 6. 异步调用方式

若底层仍使用同步 HTTP 客户端，必须放在线程中执行，避免阻塞事件循环：

```python
result = await asyncio.to_thread(_sync_call)
```

不应在 `async` 主流程中直接执行阻塞 HTTP 请求。

## 7. 响应解析

远端返回后，按两层 JSON 处理：

1. 解析接口顶层响应
2. 取 `choices[0].message.content`
3. 再对 `content` 执行一次 `json.loads(...)`
4. 使用本地 schema 做结构校验

最小成功响应示例：

```json
{
  "choices": [
    {
      "message": {
        "content": "{\"answer\":\"ok\"}"
      }
    }
  ]
}
```

解析后的内层对象为：

```json
{
  "answer": "ok"
}
```

## 8. 成功条件

至少满足以下条件才算成功：

1. `choices` 非空
2. `finish_reason` 不是 `length`
3. `message.content` 非空
4. `content` 可解析为 JSON
5. 解析结果通过本地 schema 校验

## 9. 错误处理

建议统一抛出异常，不要吞错。建议覆盖以下场景：

1. 配置缺失
2. HTTP 请求失败
3. `choices` 为空
4. 返回被截断
5. `content` 为空
6. `content` 不是合法 JSON
7. schema 校验失败

HTTPError 建议直接读取远端正文后再抛出，便于定位问题。

推荐错误消息如下：

- `ai api_key is required when provider=openai_compatible`
- `ai base_url is required when provider=openai_compatible`
- `ai model is required when provider=openai_compatible`
- `ai provider returned empty choices`
- `ai provider response was truncated`
- `ai provider returned empty content`

推荐处理方式：

```python
from urllib import error as urllib_error

try:
    ...
except urllib_error.HTTPError as exc:
    raise RuntimeError(exc.read().decode("utf-8")) from exc
```

## 10. 最小实现骨架

```python
import asyncio
import json
from urllib import error as urllib_error
from urllib import request as urllib_request


def _sync_call_openai_compatible_json(
    *,
    base_url: str,
    api_key: str,
    model: str,
    messages: list[dict[str, str]],
    max_tokens: int,
    timeout_s: float,
    temperature: float | None = None,
) -> dict:
    if not api_key:
        raise RuntimeError("ai api_key is required when provider=openai_compatible")
    if not base_url:
        raise RuntimeError("ai base_url is required when provider=openai_compatible")
    if not model:
        raise RuntimeError("ai model is required when provider=openai_compatible")

    normalized_base_url = base_url.rstrip("/")
    request_url = (
        f"{normalized_base_url}/chat/completions"
        if normalized_base_url.endswith("/v1")
        else f"{normalized_base_url}/v1/chat/completions"
    )

    request_body = {
        "model": model.strip(),
        "stream": False,
        "response_format": {"type": "json_object"},
        "max_tokens": max_tokens,
        "messages": messages,
    }

    is_reasoner_model = model.strip() == "deepseek-reasoner"
    if not is_reasoner_model and temperature is not None:
        request_body["temperature"] = temperature

    req = urllib_request.Request(
        request_url,
        method="POST",
        data=json.dumps(request_body, ensure_ascii=False).encode("utf-8"),
        headers={
            "Authorization": f"Bearer {api_key}",
            "Content-Type": "application/json",
        },
    )

    try:
        with urllib_request.urlopen(req, timeout=timeout_s) as response:
            raw_response = json.loads(response.read().decode("utf-8"))
    except urllib_error.HTTPError as exc:
        raise RuntimeError(exc.read().decode("utf-8")) from exc

    choices = raw_response.get("choices") or []
    if not choices:
        raise RuntimeError("ai provider returned empty choices")

    choice = choices[0]
    if choice.get("finish_reason") == "length":
        raise RuntimeError("ai provider response was truncated")

    message = choice.get("message") or {}
    content = message.get("content")
    if not isinstance(content, str) or not content.strip():
        raise RuntimeError("ai provider returned empty content")

    return json.loads(content)


async def call_openai_compatible_json(
    *,
    base_url: str,
    api_key: str,
    model: str,
    messages: list[dict[str, str]],
    max_tokens: int = 4096,
    timeout_s: float = 60,
    temperature: float | None = 0.2,
) -> dict:
    return await asyncio.to_thread(
        _sync_call_openai_compatible_json,
        base_url=base_url,
        api_key=api_key,
        model=model,
        messages=messages,
        max_tokens=max_tokens,
        timeout_s=timeout_s,
        temperature=temperature,
    )
```

## 11. 配置示例

```yaml
ai:
  provider: openai_compatible
  base_url: https://api.deepseek.com/v1
  model_variant: reasoner
  model: null
  api_key: null
  api_key_file: /runtime/secrets/backend_ai_api_key.txt
  request_timeout_s: 60
  max_tokens: 4096
```

这组配置的实际解析结果应为：

- `provider = openai_compatible`
- `model = deepseek-reasoner`
- `api_key` 从 `/runtime/secrets/backend_ai_api_key.txt` 读取
- 请求地址为 `https://api.deepseek.com/v1/chat/completions`

## 12. Schema 校验建议

建议对模型输出增加本地 schema 校验层。推荐做法：

1. 定义输出 DTO
2. 用 `model_validate(...)` 校验
3. 返回校验后的对象或同时返回原始结果与校验结果

推荐返回结构至少包含：

- `raw_response`
- `parsed_content`
- `validated_output`

## 13. 接入建议

推荐保留以下分层：

- 同步协议层：负责 URL、请求头、请求体、响应解析
- 异步适配层：负责将同步请求放入线程
- 校验层：负责 schema 验证与字段清洗

业务接入时仅替换：

- `system` prompt
- `user` prompt
- 输入上下文
- 输出 schema

其余协议细节保持不变。

## 14. 结论

对于“在当前项目中调用远端 AI”这件事，`docs/AI_use.md` 已经包含完整实现约定。

如果 `backend` 目录在当前项目中没有其他用途，仅从 AI 调用能力角度看，可以删除，不需要保留作为参考。
