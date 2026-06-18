# Clock Control Center Web

FastAPI service for the smart clock local control center.

Current Web pages:
- `/status` 设备状态页
- `/todos` 待办页
- `/alarms` 闹钟管理页
- `/voice` 语音设置页
- `/events` 事件历史页
- `/model` 模型对话与健康分析报告页

Current API groups:
- Todo APIs
- device config APIs
- device status APIs
- device event APIs
- model chat/report APIs

Current behavior:
- Web is the source of truth for active Todo, alarm configuration, and voice settings
- the clock only pulls unfinished Todo
- completed Todo is stored on the Web side for viewing only
- the clock periodically pulls config and reports status/events over LAN
- AI is a read-only assistant on the Web side; it does not directly modify Todo, alarms, or voice settings

Current model-page behavior:
- model chat only reads current status/config summary, open Todo summary, continuous presence duration, and hidden memory summaries
- hidden dialog memory is persisted on Web and only the latest 15 summaries are sent to AI
- the page can clear hidden dialog memory through Web API
- health analysis report is generated only when the user explicitly clicks the generate button
- only the latest successful health report is stored
- PDF export reuses the latest saved report and does not call AI again

Current AI data files:
- `data/model_dialog_memory.json`
- `data/model_presence_runtime.json`
- `data/latest_health_report.json` (created after the first successful report generation)

AI prerequisites:
- valid remote AI configuration on the Web runtime side
- readable API key file or equivalent environment override
- current device status snapshot must already be available before model chat or report generation can succeed

## Docker

Build:

```bash
docker build -t esp32s3-alarm-clock-web:latest web
```

Run:

```bash
docker run --rm -p 8080:8080 -v "$PWD/web/data:/app/data" esp32s3-alarm-clock-web:latest
```

Or use Compose from this directory:

```bash
docker compose up -d
docker compose down
```

Current clock-side target base:

```text
http://<pc-lan-ip>:8080
```

Common endpoints:

```text
GET  /api/todos
GET  /api/device/config
POST /api/device/status
POST /api/device/events
POST /api/model/chat
DELETE /api/model/memory
POST /api/model/report/generate
GET  /api/model/report
GET  /api/model/report/pdf
```
