# 智能闹钟 Web AI 只读助手实现计划

## 1. 目标

基于已确认设计，在当前 `web/` 控制中心中实现首版 AI 只读助手能力，包括：

- `/model` 页模型对话
- `/model` 页健康分析报告生成
- 最近一份报告预览与 PDF 导出
- 隐藏对话概括持久化与清空

底层 OpenAI-compatible 调用规则统一遵循：

- [docs/AI_use.md](../AI_use.md)
- [docs/AI_AST.md](../AI_AST.md)
- [docs/plans/2026-06-18-smart-clock-ai-readonly-assistant-design.md](./2026-06-18-smart-clock-ai-readonly-assistant-design.md)

## 2. 实现步骤

1. 建立 AI 后端模块骨架
- 在 `web/` 下拆出 AI 相关模块
- 区分 `client`、`context`、`chat`、`report`、`storage` 能力
- 保持 `app.py` 路由层尽量薄

2. 实现 AI 配置与 client 层
- 读取 `provider / base_url / model / api_key`
- 按 `AI_use.md` 实现 OpenAI-compatible JSON 调用
- 处理超时、HTTPError、空响应、截断与 schema 校验错误

3. 实现 AI 数据存储层
- 初始化 `model_dialog_memory.json`
- 初始化 `latest_health_report.json` 的读写逻辑
- 实现“最近 15 条隐藏概括”读取策略
- 实现“只保留最近一份报告”覆盖策略

4. 实现上下文构建层
- 从 `device_status.json` 读取当前状态
- 从 `device_config.json` 读取配置摘要
- 读取 `todos_active.json`
- 读取 `event_history.json` 并构造报告用事件摘要
- 统一生成 chat context 与 report context

5. 实现模型对话服务
- 定义 chat prompt 模板
- 定义 chat 响应 schema：`answer_text + memory_summary`
- 持久化隐藏概括
- 实现 `POST /api/model/chat`
- 实现 `DELETE /api/model/memory`

6. 实现健康报告服务
- 定义 report prompt 模板
- 定义报告输出 schema
- 保存最近一份报告
- 实现 `POST /api/model/report/generate`
- 实现 `GET /api/model/report`

7. 实现报告模板与 PDF 导出
- 定义固定 HTML 预览模板
- 将结构化报告字段映射为预览区域
- 选择并接入 PDF 导出方式
- 实现 `GET /api/model/report/pdf`
- 确保 PDF 不重复调用 AI

8. 更新 `/model` 页面前端
- 从占位页改为真实对话页
- 增加输入区、回答区、生成报告按钮、清空缓存按钮
- 增加最近报告展示区与 PDF 导出入口
- 增加发送中、生成中、失败等状态反馈
- 增加前端防重入逻辑

9. 完成测试与本地验证
- 后端单元测试
- FastAPI 接口测试
- 页面人工验收
- 成功 / 失败 / 无报告 / 清空缓存等场景验证

10. 对齐项目文档
- 补充 `web/README.md` 中 `/model` 页面说明
- 如有必要，再更新根目录 README 中关于 Web 功能的描述
- 保持 `AI_use.md`、`AI_AST.md` 与实现一致

## 3. 推荐文件布局

建议新增或调整以下文件：

- `web/app.py`
- `web/templates/model.html`
- `web/static/model.js`
- `web/static/style.css`
- `web/data/model_dialog_memory.json`
- `web/data/latest_health_report.json`
- `web/ai_client.py`
- `web/ai_context.py`
- `web/ai_chat.py`
- `web/ai_report.py`
- `web/ai_storage.py`
- `web/tests/test_ai_client.py`
- `web/tests/test_ai_chat_api.py`
- `web/tests/test_ai_report_api.py`

如果后续决定不拆独立模块文件，也至少要在代码结构上保持以上职责边界。

## 4. 关键实现约束

- AI 只能做只读分析，不得直接触发配置写操作
- 普通对话不得直接读取完整设备事件历史
- 报告生成必须由用户主动触发
- 报告失败不得覆盖旧报告
- PDF 导出不得重复调用 AI
- 用户可见聊天原文不做后端长期归档
- 隐藏概括最多带最近 `15` 条

## 5. 验证清单

- `/model` 页面可以正常发送一轮对话并显示回答
- 每轮成功对话后本地新增一条隐藏概括
- 清空缓存后隐藏概括文件被清空
- 生成报告后页面可预览结构化报告
- 生成新报告会覆盖旧报告
- 报告生成失败时旧报告仍可查看
- 有最近报告时可以导出 PDF
- 没有最近报告时 PDF 导出返回明确失败
- AI 服务异常不会影响 `/status`、`/todos`、`/alarms`、`/voice`、`/events`
- 文档与接口返回字段保持一致
