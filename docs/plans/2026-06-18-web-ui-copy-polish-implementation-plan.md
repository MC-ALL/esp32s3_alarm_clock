# Web 用户文案与报告呈现优化实现计划

## 1. 目标

在不改业务能力边界的前提下，收敛当前 Web 控制中心的用户可见文案与环境分析报告呈现。

## 2. 实现步骤

1. 更新全站模板文案
- `base.html` 顶部副标题
- `status.html`、`todos.html`、`alarms.html`、`voice.html`、`events.html`、`model.html` 页面副标题和空状态文案

2. 更新前端脚本文案
- `common.js` 通用提示语
- `todos.js`、`alarms.js`、`events.js`、`model.js` 中的空状态、确认框、失败提示、聊天角标文案

3. 收敛 `/model` 页对话区
- 输入框占位改为 `请输入疑问`
- 用户角标和助手角标改名
- 清空按钮与确认文案改名

4. 收敛 `/model` 页报告区
- 标题改为 `环境分析报告`
- 去掉英文眉题
- 生成时间只显示值本身
- 风险等级与时间分块显示

5. 增加统一时间格式函数
- 在前端增加适用于报告时间显示的格式化逻辑
- 输出 `YYYY-MM-DD HH:MM`

6. 微调样式
- 保持现有结构
- 仅优化报告头部与元信息的视觉节奏

7. 调整 PDF 生成样式
- 标题固定为 `环境分析报告`
- 生成时间不再拼进 title
- 风险等级单独换行
- 适当增加留白与版面呼吸感

8. 对齐说明文档
- 如有必要，更新 `web/README.md` 和相关说明文档中的 `/model` 页用户描述

## 3. 主要文件

- `web/templates/base.html`
- `web/templates/status.html`
- `web/templates/todos.html`
- `web/templates/alarms.html`
- `web/templates/voice.html`
- `web/templates/events.html`
- `web/templates/model.html`
- `web/static/common.js`
- `web/static/status.js`
- `web/static/todos.js`
- `web/static/alarms.js`
- `web/static/events.js`
- `web/static/model.js`
- `web/static/style.css`
- `web/ai_pdf.py`

## 4. 验收清单

- 全站不再出现明显技术实现文案
- `/model` 输入区与聊天角标已切换到用户表达
- 报告标题统一为 `环境分析报告`
- Web 报告时间格式统一为 `YYYY-MM-DD HH:MM`
- PDF 报告标题和元信息与 Web 风格更一致
