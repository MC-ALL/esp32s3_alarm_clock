# Clock Control Center Web

FastAPI service for the smart clock local control center.

Current Web pages:
- `/status` 设备状态页
- `/todos` 待办页
- `/alarms` 闹钟管理页
- `/voice` 语音设置页
- `/events` 事件历史页
- `/model` 模型对话占位页

Current API groups:
- Todo APIs
- device config APIs
- device status APIs
- device event APIs

Current behavior:
- Web is the source of truth for active Todo, alarm configuration, and voice settings
- the clock only pulls unfinished Todo
- completed Todo is stored on the Web side for viewing only
- the clock periodically pulls config and reports status/events over LAN

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
```
