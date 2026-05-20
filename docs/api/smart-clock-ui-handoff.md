# 智能闹钟界面接口交接文档

## 1. 文档目标

这份文档面向后续负责 `LVGL`、硬件驱动、传感器、网络、Todo 同步与语音模块的同学。
目标是明确：
- 当前页面实际读取哪些状态
- 当前页面会触发哪些动作
- 各页面字段如何展示
- 各模块交接时应以什么语义对齐

本文档以**当前固件实际实现**为准。

当前页面集合：
- 正常首页
- 极简显示状态
- 设置页
- 闹钟页
- 网络页
- 关机确认页
- 闹钟响铃页

补充说明：
- 页面原型只用于表达 UI 状态机与信息结构
- 运行在 `ESP32` 上时，按键扫描、Wi-Fi 状态、SNTP、环境采样、Todo 同步等都通过平台层接口接入
- 更具体的嵌入式对接契约见 [esp32-ui-integration.md](/home/long/Smart_Clock/docs/api/esp32-ui-integration.md)

---

## 2. 页面与字段

### 2.1 首页

字段清单：
- `clock.date`
- `clock.weekday`
- `clock.hhmmss`
- `clock.synced`
- `presence.detected`
- `system.soundEnabled`
- `system.audioEnabled`
- `environment.temperature`
- `environment.humidity`
- `environment.lux`
- `environment.noticeText`
- `alarm.nearest`
- `todo.items`

展示规则：
- 主视觉是完整时间
- 未校时时显示 `UNSYNC`
- 检测到人时显示 `DETECTED`
- 首页底部显示 4 个主导航提示：`Set / Alm / Net / Pwr`
- Todo 区显示前 3 条，超出显示 `+N`
- 若无 Todo，显示 `No todo`

### 2.2 极简显示状态

字段清单：
- `clock.hhmm`
- `presence.detected`
- `presence.radarHealthy`
- `presence.absenceDurationMs`
- `presence.inactivityDurationMs`

展示规则：
- 纯黑背景
- 仅显示时间
- 不显示环境、Todo、底栏按键提示
- 检测到人后退出极简状态
- 退出极简状态时可触发一次欢迎提示音

### 2.3 设置页

字段清单：
- `system.soundEnabled`
- `system.audioEnabled`
- `system.volume`
- `environment.sampleInterval`
- `alarm.repeatCount`

交互语义：
- 浏览态：上下切换配置项
- 编辑态：修改数值型配置项
- `SOUND / AUDIO` 为直接切换型
- `VOLUME / ENV SAMPLE / REPEAT` 为进入编辑型

### 2.4 闹钟页

字段清单：
- `alarm.list[]`
- `alarm.list[].hour`
- `alarm.list[].minute`
- `alarm.list[].second`
- `alarm.list[].repeat`
- `alarm.list[].enabled`
- `alarm.currentView`
- `alarm.selectedIndex`

交互语义：
- 列表态：选择 `+ NEW ALARM` 或已有闹钟
- 动作态：对当前闹钟执行 `EDIT / TOGGLE / DELETE / BACK`
- 编辑态：修改 `hour / minute / second / repeat / enabled`
- 删除确认态：确认删除当前闹钟

说明：
- 当前实现中，闹钟数据主要是本地运行态模型
- 当前字段中没有闹钟名称

### 2.5 网络页

字段清单：
- `network.wifiStarted`
- `network.wifiConnected`
- `network.ipReady`
- `network.ssid`
- `network.ip`
- `network.timeSynced`
- `network.targetAp`
- `todo.syncOk`
- `todo.lastSyncAt`

交互语义：
- 页面分为 4 个模块：`WIFI STATUS`、`ACTION`、`MY NET`、`SYNC STATUS`
- 先在模块间切换，再进入 `ACTION` 模块内部选择
- `MY NET` 只负责查看目标热点连接状态
- `SYNC STATUS` 只负责查看时间同步与 Todo 同步结果

### 2.6 关机确认页

字段清单：
- `system.powerConfirmVisible`

展示规则：
- 中间显示确认文案
- 当前实现中 `K4` 只更新确认文本，不执行真实硬件关机

### 2.7 闹钟响铃页

字段清单：
- `alarm.active.hour`
- `alarm.active.minute`
- `alarm.active.second`
- `alarm.ringing`
- `alarm.repeatRemaining`

展示规则：
- 黑底
- 中央显示当前响铃闹钟时间
- 下方显示 `PRESS ANY KEY TO STOP`
- 4 个按键全部等价为 `Stop`

---

## 3. 页面状态读取接口

建议由统一状态聚合层向 UI 提供只读状态。

示例：

```json
{
  "clock": {
    "date": "2026-05-20",
    "weekday": "TUE",
    "hhmmss": "19:30:08",
    "synced": true
  },
  "presence": {
    "detected": true,
    "radarHealthy": true,
    "absenceDurationMs": 0,
    "inactivityDurationMs": 4000
  },
  "system": {
    "soundEnabled": true,
    "audioEnabled": true,
    "volume": 6,
    "powerConfirmVisible": false,
    "currentViewMode": "home"
  },
  "environment": {
    "temperature": 26,
    "humidity": 58,
    "lux": 320,
    "sampleInterval": 1,
    "noticeText": "LIGHT LOW",
    "samplingHealthy": true
  },
  "alarm": {
    "repeatCount": 2,
    "nearest": {
      "hour": 7,
      "minute": 30,
      "second": 0,
      "repeat": true,
      "enabled": true
    },
    "list": [
      { "hour": 7, "minute": 30, "second": 0, "repeat": true, "enabled": true }
    ],
    "ringing": false
  },
  "todo": {
    "items": [
      { "id": "todo-1", "text": "10:00 前确认会议纪要并发给项目组", "done": false }
    ],
    "syncOk": true,
    "lastSyncAt": "19:25:10"
  },
  "network": {
    "wifiStarted": true,
    "wifiConnected": true,
    "ipReady": true,
    "ssid": "lbxx",
    "ip": "192.168.220.226",
    "timeSynced": true,
    "targetAp": "lbxx"
  }
}
```

---

## 4. 动作接口

### 4.1 页面导航动作

```ts
openSettings(): void
openAlarms(): void
openNetwork(): void
openPowerConfirm(): void
backHome(): void
```

说明：
- 极简模式进入 / 退出不建议作为外部动作暴露
- 极简模式应由 presence 与 inactivity 自动驱动
- 闹钟响铃页由闹钟触发逻辑自动进入

### 4.2 设置动作

```ts
setSoundEnabled(value: boolean): Result
setAudioEnabled(value: boolean): Result
setVolume(level: number): Result
setEnvSampleInterval(seconds: number): Result
setAlarmRepeatCount(count: number): Result
```

### 4.3 闹钟动作

```ts
createAlarm(payload: AlarmPayload): Result
updateAlarm(index: number, payload: AlarmPayload): Result
deleteAlarm(index: number): Result
toggleAlarm(index: number, enabled: boolean): Result
stopActiveAlarm(): Result
```

```ts
interface AlarmPayload {
  hour: number
  minute: number
  second: number
  repeat: boolean
  enabled: boolean
}
```

### 4.4 网络动作

```ts
connectNow(): Result
syncTodoNow(): Result
```

说明：
- 当前固定热点模式下，不暴露 `scanWifi / connectWifi(ssid, password) / disconnectWifi`
- 当前网络页只有手动重连和手动同步 Todo 两个动作入口

### 4.5 结果结构建议

```ts
interface Result {
  ok: boolean
  code?: string
  message?: string
}
```

建议失败时始终返回可读 `message`，用于网络页或日志输出。

---

## 5. 按键映射

### 5.1 首页
- `K1`：进入设置页
- `K2`：进入闹钟页
- `K3`：进入网络页
- `K4`：进入关机确认页

### 5.2 设置页
浏览态：
- `K1`：上移
- `K2`：下移
- `K3`：修改 / 进入编辑
- `K4`：返回首页

编辑态：
- `K1`：增加数值
- `K2`：减少数值
- `K3`：保存
- `K4`：取消

### 5.3 闹钟页
列表态：
- `K1`：上移
- `K2`：下移
- `K3`：选择当前项
- `K4`：返回首页

动作态：
- `K1`：上移
- `K2`：下移
- `K3`：执行当前动作
- `K4`：返回列表态

删除确认态：
- `K1`：取消
- `K4`：删除

编辑态：
- `K1`：减小 / 切换当前字段
- `K2`：增大 / 切换当前字段
- `K3`：下一字段 / 保存
- `K4`：取消

### 5.4 网络页
模块浏览态：
- `K1`：切上一个模块
- `K2`：切下一个模块
- `K3`：选择
- `K4`：返回首页

动作选择态：
- `K1`：上移
- `K2`：下移
- `K3`：执行动作
- `K4`：返回模块浏览态

### 5.5 关机确认页
- `K1`：返回首页
- `K4`：确认

### 5.6 闹钟响铃页
- `K1`：停止
- `K2`：停止
- `K3`：停止
- `K4`：停止

---

## 6. 当前实现限制

1. 关机页尚未接真实关机动作
2. 网络页仅支持固定热点模式，不支持自由切换热点
3. `MY NET` 不是扫描结果列表，只是目标热点连接状态查看页
4. Todo 同步依赖局域网 Web 服务，设备本身不负责编辑 Todo 文本
5. 闹钟当前没有名称字段
6. `ENV SAMPLE` 尚未形成完整任务级动态重配置接口
