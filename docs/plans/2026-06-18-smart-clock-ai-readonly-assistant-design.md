# 智能闹钟 Web AI 只读助手与健康报告设计

## 1. 目标

本设计定义当前 `Smart Clock` 项目中远端 AI 在 `Web` 侧的首版业务接入方式。

本轮 AI 功能仅面向 `web/` 控制中心，不进入设备固件主链路。AI 在业务上承担两个只读能力：

- 模型对话：用户在 `/model` 页面向远端 AI 发起垂直问答
- 健康分析报告：用户点击按钮后，基于当前设备状态生成一份结构化健康分析报告，并支持在线预览与 PDF 导出

AI 接入协议、配置解析、OpenAI-compatible 调用规则统一遵循 [docs/AI_use.md](../AI_use.md)。

## 2. 范围

### 2.1 本轮范围

- 单设备模式，默认当前设备为 `clock-001`
- `Web` 侧 AI 只读助手
- `/model` 页模型对话能力
- `/model` 页健康分析报告生成能力
- 对话隐藏概括持久化
- 最近一份报告持久化
- 在线报告预览
- PDF 导出
- 本地 JSON 存储
- 后端 schema 校验与错误处理

### 2.2 不在本轮范围

- AI 直接修改 Todo / 闹钟 / 语音设置
- 多设备选择与多设备聚合报告
- 用户账号体系
- 用户可见聊天原文长期归档
- 多份报告历史存档
- 自动定时生成报告
- 泛用聊天助手
- 将完整事件历史直接提供给普通对话链路

## 3. 核心决策

### 3.1 AI 角色定位

AI 是 `只读助手`，不具备直接执行配置修改的权限。

AI 可读取当前 Web 端已经拥有的数据快照，并输出：

- 面向用户的对话回答
- 面向系统的隐藏对话概括
- 面向报告模板的结构化报告字段

### 3.2 能力拆分

AI 功能按 `共享上下文层 + 专用能力接口` 实现。

共享层负责收集和裁剪业务上下文；上层拆成两个能力：

- `chat service`
- `report service`

两者共享同一套 AI client，但拥有各自独立的：

- prompt 模板
- 输出 schema
- 持久化规则
- 前端交互路径

### 3.3 单设备假设

首版仅面向当前唯一闹钟设备，不引入设备选择器。

### 3.4 对话记忆策略

用户可见聊天原文只保留在当前页面会话中，不做后端长期存档。

后端长期保存的是 `隐藏对话概括`：

- 每轮成功对话后追加一条概括
- 每次发给 AI 时最多带最近 `15` 条概括
- `/model` 页面提供“清空对话缓存”按钮
- 用户确认后清空本地隐藏概括记录

### 3.5 报告持久化策略

健康分析报告仅在用户点击“生成报告”时调用 AI。

系统只保存最近一份报告：

- 生成成功后覆盖旧报告
- 生成失败时保留旧报告
- PDF 导出基于最近一次成功报告，不重新调用 AI

### 3.6 静坐时长定义

首版“静坐时长”以 `连续在位时长` 作为业务代理值，不做姿态识别。

### 3.7 文风要求

- 模型对话：简洁、自然、略带风趣
- 健康报告：专业、严谨、稍详尽但不说废话

## 4. 架构与组件边界

### 4.1 AI Config / Client

负责：

- 读取 AI 配置
- 解析 `provider / base_url / model / api_key`
- 发起 OpenAI-compatible 请求
- 处理超时与 HTTP 错误
- 解析顶层响应与内层 JSON
- 执行本地 schema 校验

该层不承载任何业务 prompt。

### 4.2 AI Context Builder

负责从 Web 侧现有数据源中整理统一上下文。

可读取的数据包括：

- 当前设备状态快照
- 当前配置摘要
- 连续在位时长
- 当前未完成 Todo
- 隐藏对话概括
- 报告专用的设备事件摘要

该层输出稳定、低噪声的结构化上下文对象，供上层服务使用。

### 4.3 Chat Service

负责：

- 接收用户输入
- 读取最近 `15` 条隐藏概括
- 组装对话 prompt
- 调用远端 AI
- 校验返回的 `answer_text` 与 `memory_summary`
- 持久化 `memory_summary`
- 返回用户可见回答

### 4.4 Report Service

负责：

- 仅在用户显式触发时执行
- 组装报告专用上下文
- 调用远端 AI
- 校验返回的结构化报告字段
- 保存最近一份报告
- 供在线预览和 PDF 导出复用

### 4.5 Storage Layer

沿用当前 `web/data/` JSON 文件存储模式，不引入数据库。

### 4.6 PDF Renderer

基于最近一份已保存报告生成 PDF。

PDF 渲染不重新请求 AI。

## 5. 数据模型与持久化

### 5.1 新增数据文件

建议新增：

- `web/data/model_dialog_memory.json`
- `web/data/model_presence_runtime.json`
- `web/data/latest_health_report.json`

### 5.2 对话隐藏记忆

`model_dialog_memory.json` 保存后端内部可见的隐藏概括。

建议结构：

```json
[
  {
    "id": "mem-001",
    "created_at": "2026-06-18T20:30:00+08:00",
    "summary_text": "用户询问当前环境是否适合久坐办公，已基于当前温湿度、光照、连续在位时长给出简洁建议。",
    "source_user_input_excerpt": "我现在这个环境适合继续工作吗？",
    "source_answer_excerpt": "环境基本可用，但你已经连续在位较久，建议先起身活动。"
  }
]
```

其中只有 `summary_text` 会进入后续 AI 上下文，其他字段主要用于调试与排查。

### 5.3 最近一份健康报告

`latest_health_report.json` 仅保存最近一次成功生成的报告。

建议结构：

```json
{
  "generated_at": "2026-06-18T20:45:00+08:00",
  "render_version": "v1",
  "input_snapshot": {
    "device_status": {},
    "config_summary": {},
    "continuous_presence_duration_s": 10800,
    "open_todos": [],
    "report_event_summary": {}
  },
  "report_fields": {
    "report_title": "办公健康分析报告",
    "risk_level": "medium",
    "overall_summary": "当前环境基本可工作，但连续在位时长偏长，建议尽快安排短时活动。",
    "environment_analysis": "当前温湿度处于可接受范围，光照略弱，长时间阅读可能增加眼疲劳。",
    "sedentary_analysis": "已连续在位约 3 小时，久坐负担明显上升。",
    "todo_and_routine_advice": "待办较少，适合先安排一次短休息，再回到剩余任务。",
    "improvement_actions": [
      "先离开座位活动 5 到 10 分钟",
      "补充环境照明，避免长时间低照度用眼",
      "下一个工作段控制在 45 到 60 分钟内"
    ],
    "key_findings": [
      "连续在位时长偏长",
      "环境光照偏弱"
    ]
  }
}
```

### 5.4 用户可见原文不长期保存

首版不保存用户可见完整问答历史。

这样可以：

- 控制 prompt 长度
- 降低持久化复杂度
- 避免产品形态偏向通用聊天工具

## 6. 上下文边界与 Prompt 规则

### 6.1 模型对话可读数据

普通对话只读取：

- 当前设备状态快照
- 当前配置摘要
- 连续在位时长
- 当前未完成 Todo
- 最近最多 `15` 条隐藏概括

普通对话不读取设备事件历史。

### 6.2 健康报告可读数据

报告读取：

- 当前设备状态快照
- 当前配置摘要
- 连续在位时长
- 当前未完成 Todo
- 少量关键设备事件的压缩摘要

建议首版由后端基于最近 `10` 条关键事件构造一个结构化 `report_event_summary`，而不是把整段原始事件列表直接发给 AI。

### 6.3 Prompt 分离

必须使用两套独立 prompt：

- `chat_system_prompt`
- `report_system_prompt`

普通对话 prompt 要明确：

- 垂直助手定位
- 只围绕环境、作息、Todo、健康建议展开
- 回答简洁风趣
- 不伪造数据
- 不输出 Markdown 包装的 JSON

报告 prompt 要明确：

- 专业、严谨、克制
- 只依据输入数据判断
- 不夸大风险
- 只输出结构化 JSON

### 6.4 上下文裁剪原则

- 隐藏概括最多 `15` 条
- Todo 只传必要摘要
- 配置只传 AI 真正需要的摘要字段
- 报告事件只传压缩后的少量摘要
- 缺失字段显式标记为 unavailable，而不是伪造默认值

## 7. AI 输出 Schema

### 7.1 对话返回结构

```json
{
  "answer_text": "建议先起身活动几分钟，再回来继续工作。你现在更像高效办公，不像耐力赛。",
  "memory_summary": "用户询问当前环境是否适合继续工作，回答中指出环境可用但连续在位较长，建议短时活动后再继续。"
}
```

### 7.2 报告返回结构

```json
{
  "report_title": "办公健康分析报告",
  "risk_level": "medium",
  "overall_summary": "当前环境基本可工作，但连续在位时长偏长，建议尽快安排短时活动。",
  "environment_analysis": "...",
  "sedentary_analysis": "...",
  "todo_and_routine_advice": "...",
  "improvement_actions": ["..."],
  "key_findings": ["..."]
}
```

## 8. 数据流

### 8.1 模型对话链路

1. 前端提交用户输入
2. 后端读取状态、配置、连续在位时长、Todo、隐藏概括
3. 后端组装对话 prompt
4. 调用远端 AI
5. 校验返回 JSON
6. 追加保存 `memory_summary`
7. 返回 `answer_text` 给前端

### 8.2 健康报告链路

1. 用户点击生成报告
2. 后端读取状态、配置、连续在位时长、Todo、事件摘要
3. 组装报告 prompt
4. 调用远端 AI
5. 校验报告 JSON
6. 保存最近一份报告
7. 返回报告结构给前端预览

### 8.3 清空缓存链路

1. 用户点击“清空对话缓存”
2. 前端弹出确认
3. 后端清空 `model_dialog_memory.json`
4. 前端同步清空当前页会话展示

### 8.4 PDF 导出链路

1. 用户点击导出 PDF
2. 后端读取最近一份报告
3. 套用固定模板
4. 返回 PDF 文件流

## 9. API 草案

### 9.1 `POST /api/model/chat`

请求：

```json
{
  "message": "我现在这个环境适合继续工作吗？"
}
```

成功响应：

```json
{
  "answer_text": "建议先起身活动几分钟，再回来继续工作。你现在更像高效办公，不像耐力赛。"
}
```

### 9.2 `DELETE /api/model/memory`

成功响应：

```json
{
  "ok": true
}
```

### 9.3 `POST /api/model/report/generate`

请求体可为空对象：

```json
{}
```

成功响应：

```json
{
  "generated_at": "2026-06-18T20:45:00+08:00",
  "render_version": "v1",
  "report_fields": {
    "report_title": "办公健康分析报告",
    "risk_level": "medium",
    "overall_summary": "...",
    "environment_analysis": "...",
    "sedentary_analysis": "...",
    "todo_and_routine_advice": "...",
    "improvement_actions": ["..."],
    "key_findings": ["..."]
  }
}
```

### 9.4 `GET /api/model/report`

- 若存在最近报告，返回最近一份报告
- 若不存在，返回 `404`

### 9.5 `GET /api/model/report/pdf`

- 基于最近一份成功报告导出 PDF
- 若当前没有可导出的报告，返回 `404`

## 10. 错误处理与降级

### 10.1 全局原则

AI 失败不能影响现有：

- `/status`
- `/todos`
- `/alarms`
- `/voice`
- `/events`
- 设备配置拉取、状态上报、事件上报

### 10.2 对话失败

- 本轮不展示伪造回答
- 不保存隐藏概括
- 返回简明错误提示
- 已显示的旧回答保留

### 10.3 报告失败

- 不覆盖旧报告
- 若旧报告存在，仍允许查看和导出旧报告
- 不生成半成品 PDF

### 10.4 缺数据时的策略

可降级：

- 无 Todo
- 无事件
- 无历史隐藏概括

不可降级：

- 无基础设备状态
- 无基础配置摘要
- AI 配置缺失

### 10.5 防重复提交

- 对话发送进行前端防重入
- 报告生成进行前端防重入
- 导出 PDF 仅对已有报告开放

## 11. 测试与验收

### 11.1 单元测试

- AI 配置解析
- URL 拼接
- 对话 schema 校验
- 报告 schema 校验
- 隐藏概括持久化
- 最近报告覆盖保存
- 清空缓存逻辑

### 11.2 接口测试

- `POST /api/model/chat` 成功 / 失败
- `DELETE /api/model/memory`
- `POST /api/model/report/generate` 成功 / 失败
- `GET /api/model/report` 有报告 / 无报告
- `GET /api/model/report/pdf` 有报告 / 无报告

### 11.3 人工验收

- 对话语气符合“简洁风趣”
- 报告口吻专业、无废话
- 失败时旧报告仍可用
- 清空缓存后不再带旧记忆
- 在线预览与 PDF 内容一致

## 12. 实施边界

首版明确不做：

- AI 直接改配置
- 多设备
- 聊天原文长期归档
- 报告历史
- 自动生成报告
- 通用聊天模式
- 对普通对话透传完整事件历史
