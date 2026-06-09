# Smart Clock Web/Device Control Center Design

## 1. Goal

This design expands the current Todo-only LAN web service into a lightweight control center for:

- active Todo management
- alarm management
- voice settings management
- device status overview
- event history display
- future model gateway placeholder

The design keeps `Web` as the source of truth for editable configuration, while the device remains the runtime executor and status producer.

## 2. Scope

In scope:

- Web as source of truth for active Todo, alarm config, and voice settings
- device-side immediate write-back when local Todo/alarm/voice changes succeed
- device status reporting
- device event reporting
- completed Todo archive on Web only
- simplified event history filtering
- placeholder model gateway contract

Out of scope:

- direct model integration
- cloud deployment
- account system
- event history storage on device
- completed Todo synchronization back to device

## 3. Core Decisions

### 3.1 Source Of Truth

- `Web` is the final source of truth for:
  - active Todo
  - alarm configuration
  - voice settings
- the device may edit these locally, but every successful local edit must immediately request a matching Web-side update
- later periodic pulls only re-sync the Web source of truth back to the device

### 3.2 Sync Topology

Use three channels:

1. configuration pull: `Web -> Device`
2. status report: `Device -> Web`
3. event report: `Device -> Web`

This avoids per-page polling and keeps state, config, and events separated.

### 3.3 Sync Intervals

Recommended defaults:

- configuration pull: `30s`
- status report: `10s`
- event report: immediate on event

The shortest default periodic interval should be `10s`, and only for status reporting.

## 4. Data Ownership

### 4.1 Web Side

Web owns:

- active Todo list
- completed Todo archive
- alarm configuration
- voice settings
- latest device status
- event history

### 4.2 Device Side

Device owns runtime execution only:

- current active Todo cache
- current alarm runtime config
- current voice runtime config
- current sampled environment state
- current presence state

The device does **not** store:

- completed Todo archive
- event history

## 5. Todo Semantics

### 5.1 Active Todo

- the device only pulls unfinished Todo items
- the device only displays unfinished Todo items

### 5.2 Complete

When the device completes a Todo:

- Web removes it from the active Todo list
- Web writes it into the completed Todo archive
- the device no longer receives it on later pulls

### 5.3 Delete

When the device deletes a Todo:

- Web permanently removes it
- it does not enter the completed Todo archive

### 5.4 Completed Todo On Web

- completed Todo is visible on the Web Todo page in a dedicated list
- completed Todo is read-only
- completed Todo cannot be restored to unfinished state in the first version

## 6. Channel Design

### 6.1 Configuration Pull

`GET /api/device/config`

Payload contains:

- active Todo only
- alarm configuration
- voice settings
- configuration version
- updated timestamp

Recommended response shape:

```json
{
  "config_version": 12,
  "updated_at": "2026-06-09T20:00:00+08:00",
  "todos_active": [
    {
      "id": "todo-001",
      "text": "Confirm meeting notes",
      "updated_at": "2026-06-09T19:40:00+08:00"
    }
  ],
  "alarms": [
    {
      "id": "alarm-001",
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

### 6.2 Status Report

`POST /api/device/status`

Reported every `10s`.

Payload contains:

- device id
- timestamp
- online state
- temperature
- humidity
- lux
- presence detected

This endpoint only updates the latest status snapshot on Web.

### 6.3 Event Report

`POST /api/device/events`

Triggered immediately when a device event occurs.

First-version event types:

- `todo_completed`
- `todo_deleted`
- `alarm_triggered`
- `rest_reminder_triggered`
- `env_alert_triggered`
- `welcome_played`
- `todo_sync_up_played`

Recommended payload fields:

- device id
- event timestamp
- event type
- optional business id such as `todo_id`
- temperature
- humidity
- lux
- presence detected

### 6.4 Device Write-Back Endpoints

Device-side local edits should immediately request Web updates:

- `PUT /api/device/alarms`
- `PUT /api/device/voice-settings`
- `POST /api/todos/{id}/complete`
- `DELETE /api/todos/{id}`

The first version should avoid a generic `done=true/false` Todo update API for device use, because complete and delete are now distinct business actions.

## 7. Web Data Stores

First-version storage can remain JSON-based, but should split by responsibility:

- `data/todos_active.json`
- `data/todos_completed.json`
- `data/device_status.json`
- `data/event_history.json`
- `data/device_config.json`

This keeps future migration to SQLite straightforward.

## 8. Web Pages

Recommended Web pages:

1. device status page
2. active Todo page
3. alarm management page
4. event history page
5. voice settings page
6. model dialog page placeholder

### 8.1 Device Status Page

Shows:

- online/offline based on heartbeat freshness
- temperature
- humidity
- lux
- presence detected

### 8.2 Active Todo Page

Shows:

- active Todo list
- completed Todo archive list for view only

### 8.3 Alarm Management Page

Edits the same fields the device already supports.

### 8.4 Event History Page

The first version only needs:

- list display
- time range filter
- event type filter

### 8.5 Voice Settings Page

Edits device voice-related configuration stored in Web source of truth.

### 8.6 Model Dialog Page

Only a placeholder contract in the first version.

- no real model integration
- no event history in model context
- input context only uses current configuration and current status summary
- output must remain plain text, concise, and format-restricted

## 9. Conflict Policy

- `Web` wins on truth
- all configuration write-back requests should carry `config_version`
- if Web detects stale writes, it returns `409`
- after `409`, the device must re-pull configuration and discard the stale local write attempt

## 10. Failure Handling

### 10.1 Configuration Pull Failure

- keep last valid local config
- mark sync failure in UI/log
- retry on next cycle

### 10.2 Status Report Failure

- do not build a large retry backlog
- next report overwrites the latest status snapshot

### 10.3 Event Report Failure

- keep only a small in-memory resend queue on device
- no event history persistence on device
- drop oldest buffered events if the queue is full

### 10.4 Local Edit Write-Back Failure

- the device must not treat the change as final Web truth
- it should surface failure and refresh from Web on the next successful pull

## 11. Testing Boundaries

### 11.1 Web API Tests

- configuration snapshot returns only active Todo
- complete moves Todo from active to completed
- delete permanently removes Todo
- stale config version returns `409`

### 11.2 Device Integration Tests

- periodic configuration pull refreshes local runtime state
- local Todo/alarm/voice edits immediately write back to Web
- failed write-back is not treated as final success
- status report failure does not break runtime
- event queue drops oldest entries when full

### 11.3 End-To-End Tests

- Web alarm edit becomes visible on device within `30s`
- device alarm edit becomes visible on Web immediately after success
- device Todo complete creates a completed Web record and removes it from later device pulls
- device Todo delete removes it entirely
- status page reflects device heartbeat and sensor updates within `10s`

## 12. Migration Notes

The current Todo-only LAN web service is a valid first-stage base, but the next-stage architecture should no longer be described as one-way Todo sync only.

The approved expansion path is:

- Todo-only Web -> Web control center
- one-way pull -> three-channel sync
- single active Todo list -> active + completed archive
- local device runtime -> Web-managed configuration truth
