# Clock Web Design

## 1. Goal

This document describes the current LAN Web design that now serves as a lightweight control center for the smart clock.

The current Web side is no longer only a Todo text editor. It now covers:

- active Todo management
- completed Todo archive viewing
- alarm configuration management
- voice settings management
- latest device status viewing
- event history viewing
- model dialog placeholder

The approved architecture still keeps the Web side as the source of truth for editable configuration.

## 2. Scope

In scope:
- LAN deployment model
- backend and frontend responsibility split
- active/completed Todo model
- alarm and voice settings model
- device configuration pull API
- device status report API
- device event report API
- local JSON persistence design
- failure handling expectations

Out of scope:
- cloud sync
- account/login system
- public internet access
- event history storage on device
- real model integration

## 3. Role Split

### 3.1 Web Responsibilities

The Web service is the source of truth for:
- active Todo
- completed Todo archive
- alarm configuration
- voice settings

The Web service also stores:
- latest device status
- event history

### 3.2 Device Responsibilities

The clock is responsible for:
- periodically pulling configuration from Web
- caching the latest active Todo locally
- executing alarms and reminders
- reporting device status
- reporting device events

The clock does not store:
- completed Todo archive
- event history

## 4. Sync Topology

The current expansion direction uses three channels:

1. configuration pull: `Web -> Clock`
2. status report: `Clock -> Web`
3. event report: `Clock -> Web`

Recommended default intervals:
- configuration pull: `30s`
- status report: `10s`
- event report: immediate on event

## 5. Deployment Model

The Web service runs on the user's own computer.
The smart clock and the computer must be on the same LAN.

Example:
- computer IP: LAN-assigned local address
- service port: `8080`
- clock target base: `http://<pc-lan-ip>:8080`

If the service is down:
- the clock keeps the last successful active Todo and configuration cache
- status/event reports may fail and retry later according to device policy

## 6. Data Model

### 6.1 Active Todo

```json
{
  "id": "todo-001",
  "text": "10:00 前确认会议纪要并发给项目组",
  "done": false,
  "updated_at": "2026-06-09T10:30:00+08:00"
}
```

### 6.2 Completed Todo

```json
{
  "id": "todo-001",
  "text": "10:00 前确认会议纪要并发给项目组",
  "done": true,
  "updated_at": "2026-06-09T10:35:00+08:00",
  "completed_at": "2026-06-09T10:35:00+08:00"
}
```

Completed Todo is Web-only and read-only.
It is not sent back to the device.

### 6.3 Device Config Snapshot

```json
{
  "config_version": 12,
  "updated_at": "2026-06-09T20:00:00+08:00",
  "todos_active": [
    {
      "id": "todo-001",
      "text": "确认例会纪要",
      "updated_at": "2026-06-09T19:40:00+08:00"
    }
  ],
  "alarms": [
    {
      "hour": 7,
      "minute": 30,
      "repeat": true,
      "enabled": true,
      "voice": true
    }
  ],
  "voice_settings": {
    "todo_voice_on": true,
    "alarm_voice_on": true,
    "env_voice_on": true,
    "env_alert_on": true,
    "home_hour_chime_on": true
  }
}
```

### 6.4 Device Status Snapshot

```json
{
  "device_id": "clock-001",
  "updated_at": "2026-06-09T20:00:10+08:00",
  "online": true,
  "temperature_c": 26.0,
  "humidity_percent": 52.0,
  "lux": 180.0,
  "presence_detected": true
}
```

### 6.5 Event History Item

```json
{
  "device_id": "clock-001",
  "event_at": "2026-06-09T20:01:00+08:00",
  "event_type": "todo_completed",
  "todo_id": "todo-001",
  "temperature_c": 26.0,
  "humidity_percent": 52.0,
  "lux": 180.0,
  "presence_detected": true
}
```

## 7. Current API

### 7.1 Todo APIs

- `GET /api/todos`
  - returns active Todo and completed Todo archive for Web UI use
- `POST /api/todos`
  - create active Todo
- `PUT /api/todos/reorder`
  - reorder active Todo
- `PUT /api/todos/{id}`
  - update active Todo text
- `POST /api/todos/{id}/complete`
  - move active Todo into completed archive
- `DELETE /api/todos/{id}`
  - permanently delete active Todo

### 7.2 Device Config APIs

- `GET /api/device/config`
- `PUT /api/device/config`
- `PUT /api/device/alarms`
- `PUT /api/device/voice-settings`

### 7.3 Device Status APIs

- `POST /api/device/status`
- `GET /api/device/status`

### 7.4 Device Event APIs

- `POST /api/device/events`
- `GET /api/device/events`

Current simplified event filtering:
- by `event_type`
- by `date_from`
- by `date_to`
- by `limit`

## 8. Storage Layout

Current JSON storage layout:
- `data/todos_active.json`
- `data/todos_completed.json`
- `data/device_status.json`
- `data/event_history.json`
- `data/device_config.json`

Legacy `data/todos.json` may still exist as an import source, but the current runtime model uses the new split files.

## 9. Key Business Rules

1. Web is the source of truth for active Todo, alarm configuration, and voice settings.
2. The clock only pulls unfinished Todo.
3. Completed Todo is archived on Web and not sent to the clock again.
4. Device-side Todo complete/delete should immediately update the Web source on success.
5. Device-side alarm/voice edits should immediately write back to Web.
6. Event history exists only on Web.

## 10. Current Positioning

This Web side should now be understood as a lightweight local control center rather than only a Todo text editor.

The more detailed control-center evolution design is documented in:
- `docs/plans/2026-06-09-web-device-control-center-design.md`
