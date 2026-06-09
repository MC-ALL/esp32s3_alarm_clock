# ESP32 UI Integration API

## 1. 目标

本文档说明当前 `ESP32` 固件与本地 Web 控制中心之间已经落地或已经明确收敛的接口语义。

当前系统不再只是“设备拉 Todo 列表”的单通道结构，而是朝三通道同步演进：

- 配置快照拉取
- 状态上报
- 事件上报

## 2. 当前实现结论

### 2.1 UI 只在本地运行

当前 UI 运行在 `ESP32` 本地，由：
- `display_service`
- `ui_model`

共同完成。

### 2.2 当前真实页面主线

当前主页面为：
- `HOME`
- `ALARM`
- `TODO`
- `ENV`
- `WIFI`
- `LOW_CLOCK`

### 2.3 Todo 已不只是只读展示

当前固件已经支持：
- 手动同步 Todo
- 标记 Todo 完成
- 删除 Todo

并且这些动作会立即请求 Web 更新源数据。

## 3. 当前真实输入接口

### 3.1 按键输入

按键最终由：
- `ui_model_handle_key_press(size_t key_index)`

消费。

当前仍以单次按键事件为主，不区分长按和连按。

### 3.2 环境输入

通过：
- `environment_service_get_snapshot()`

提供温度、湿度、光照及有效位。

### 3.3 在位输入

通过：
- `presence_service_get_status()`

提供是否有人、雷达健康状态、是否使用 `OUT` 回退。

### 3.4 网络状态输入

通过：
- `net_service_get_status()`

提供 WiFi 与时间同步状态。

### 3.5 Todo 输入

通过：
- `net_service_get_todo_snapshot()`

提供当前本地未完成 Todo 快照。

### 3.6 配置快照输入

通过：
- `net_service_get_device_config_snapshot()`

提供最近一次从 Web 拉取到的配置快照信息，包括：
- `config_version`
- `updated_at`
- 当前生效的闹钟配置
- 当前生效的语音配置

## 4. 当前真实输出动作

### 4.1 Todo 动作

当前接口：

```c
int net_service_request_todo_sync_now(void);
int net_service_request_todo_set_done(const char *todo_id, bool done);
int net_service_request_todo_delete(const char *todo_id);
```

当前语义：
- 完成 Todo：请求 Web 将该项移入 completed archive
- 删除 Todo：请求 Web 彻底删除该项
- 设备后续只继续持有未完成 Todo

### 4.2 配置回写动作

当前接口：

```c
int net_service_request_push_alarm_settings(const app_settings_t *settings);
int net_service_request_push_voice_settings(const app_settings_t *settings);
```

当前语义：
- 本地修改闹钟后，立即请求 Web 更新闹钟真源
- 本地修改语音设置后，立即请求 Web 更新语音设置真源

### 4.3 事件上报动作

当前接口：

```c
int net_service_request_report_event(const char *event_type, const char *todo_id);
```

当前已接入或已预留的事件类型包括：
- `todo_completed`
- `todo_deleted`
- `alarm_triggered`
- `env_alert_triggered`
- `rest_reminder_triggered`
- `todo_sync_up_played`
- `welcome_played`

### 4.4 网络动作

当前接口：

```c
int net_service_request_connect_now(void);
```

## 5. Web 控制中心对接重点

### 5.1 配置快照拉取

当前设备端已切换到从：
- `GET /api/device/config`

拉取配置快照。

快照内容包括：
- 未完成 Todo
- 闹钟配置
- 语音设置
- `config_version`

### 5.2 状态上报

当前设备端已接入：
- `POST /api/device/status`

上报内容包括：
- 在线状态
- 温度
- 湿度
- 光照
- 是否有人

### 5.3 事件上报

当前设备端已接入：
- `POST /api/device/events`

设备不存储事件历史，只负责把事件告诉 Web。

## 6. 当前实现限制

1. 当前配置快照解析仍为轻量实现，不是完整 JSON 解析器。
2. 设备端目前只存储未完成 Todo，不存 completed archive。
3. 事件历史只存在于 Web 后端和前端展示中。
4. 更完整的控制中心扩展设计，应参考 `docs/plans/2026-06-09-web-device-control-center-design.md`。
