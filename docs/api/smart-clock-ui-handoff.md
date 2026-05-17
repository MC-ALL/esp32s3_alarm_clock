# 智能闹钟界面接口交接文档

## 1. 文档目标

这份文档面向后续负责 `LVGL`、硬件驱动、传感器、网络与语音模块的同学。
目标是明确：
- 页面需要读取哪些状态
- 页面会发出哪些动作
- 各页面字段如何展示
- 各类异常和空数据如何降级显示

当前 HTML 原型对应的页面：
- 正常显示首页
- 极简显示页
- 设置页
- 闹钟页
- 网络页
- 关机确认页
- 关机态

补充说明：
- 页面原型用于表达 UI 状态机与信息结构
- 真正运行在 `ESP32` 上时，硬件驱动、本地服务同步、按键扫描、雷达采样都应通过平台层接口接入
- 更具体的嵌入式对接契约见 [esp32-ui-integration.md](/home/long/Smart_Clock/docs/api/esp32-ui-integration.md)

## 2. 页面与字段

### 2.1 首页

字段清单：
- `clock.date`：日期，格式 `YYYY-MM-DD`
- `clock.weekday`：星期文本，如 `周四`
- `clock.hhmm`：时分，格式 `HH:MM`
- `clock.ss`：秒，格式 `SS`
- `clock.synced`：是否已校时
- `system.soundMuted`：是否静音
- `system.audioEnabled`：是否开启语音
- `environment.temperature`
- `environment.humidity`
- `environment.lux`
- `voice.currentPromptText`
- `alarm.nearest`
- `todo.items`

展示规则：
- `HH:MM` 为主视觉
- 秒数小一号显示在右侧
- 日期和星期为小号辅助信息
- 右上显示 `sound` 和 `audio` 状态，页面展示为 `ON/OFF`
- 环境参数固定为一行，按 `图标 + 标识 + 数值` 紧凑展示
- 语音提示显示 1 到 2 行
- 最近闹钟展示时间、星期方案、是否重复
- todo 列表显示 4 到 5 条，超出显示 `+N more`

### 2.2 极简显示页

字段清单：
- `clock.hhmm`
- `clock.ss`

展示规则：
- 纯黑背景
- 仅显示时间
- 不显示任何提示条、环境数据、待办、按键说明
- 雷达重新检测到人后返回首页
- 如果用户在极简模式下再次操作按键，也可立即返回首页

### 2.3 设置页

字段清单：
- `system.soundMuted`
- `system.audioEnabled`
- `system.volume`
- `environment.sampleInterval`
- `voice.repeatCount`

交互语义：
- 浏览态：上下选择配置项
- 编辑态：调整当前配置项

### 2.4 闹钟页

字段清单：
- `alarm.list[]`
- `alarm.list[].id`
- `alarm.list[].hour`
- `alarm.list[].minute`
- `alarm.list[].second`
- `alarm.list[].weekdays[]`
- `alarm.list[].repeat`
- `alarm.list[].enabled`

交互语义：
- 列表态：查看列表，选择“新建闹钟”或某条已有闹钟
- 动作态：对当前闹钟执行 `编辑 / 启用或停用 / 删除`
- 编辑态：修改时分秒、星期方案、重复、启用状态
- 删除确认态：确认是否删除当前闹钟
- `repeat=false` 且触发完成的闹钟，不再出现在 `alarm.list`

### 2.5 网络页

字段清单：
- `network.wifiConnected`
- `network.ssid`
- `network.ip`
- `network.rssi`
- `network.scanResults[]`
- `network.ntpSyncStatus`
- `network.ntpSyncedAt`
- `network.todoSyncStatus`
- `network.todoSyncedAt`
- `network.lastError`

交互语义：
- 页面分为 4 个大模块：`WiFi状态`、`动作`、`扫描结果`、`同步状态`
- 先在大模块之间切换，再进入模块内部选择
- `扫描结果` 内选中某个热点后，`K3` 默认执行连接
- `动作` 模块仅保留：扫描、断开、校时、同步 Todo

### 2.6 关机确认页

字段清单：
- `system.powerState`

展示规则：
- 中间大字显示确认文案
- `K1` 返回首页
- `K4` 确认关机

## 3. 页面状态读取接口

建议由一个统一状态聚合层向 UI 提供只读状态。

示例：

```json
{
  "clock": {
    "timestamp": 1778761514000,
    "date": "2026-05-14",
    "weekday": "周四",
    "hhmm": "20:25",
    "ss": "14",
    "synced": true
  },
  "system": {
    "soundMuted": false,
    "audioEnabled": true,
    "volume": 7,
    "powerState": "on",
    "currentViewMode": "home"
  },
  "environment": {
    "temperature": 26,
    "humidity": 58,
    "lux": 320,
    "sampleInterval": 30,
    "lastUpdatedAt": 1778761498000,
    "samplingHealthy": true
  },
  "presence": {
    "detected": true,
    "absenceDurationMs": 8000,
    "inactiveDurationMs": 3000,
    "thresholdMs": 30000,
    "radarHealthy": true,
    "absentSince": null
  },
  "voice": {
    "currentPromptText": "提醒：环境光偏低，注意开灯。",
    "lastPromptType": "environment",
    "speaking": false,
    "repeatCount": 2
  },
  "alarm": {
    "nearest": {
      "id": "alarm-1",
      "hour": 7,
      "minute": 30,
      "second": 0,
      "weekdays": [1, 3, 5],
      "repeat": true,
      "enabled": true
    },
    "list": []
  },
  "todo": {
    "items": [
      { "id": "todo-1", "text": "10:00 前确认会议纪要并发给项目组" }
    ],
    "syncStatus": "ok",
    "lastSyncAt": 1778761451000
  },
  "network": {
    "wifiConnected": true,
    "ssid": "Office-Nest",
    "ip": "192.168.1.72",
    "rssi": -48,
    "scanResults": [
      { "ssid": "Office-Nest", "rssi": -48, "secure": true }
    ],
    "ntpSyncStatus": "ok",
    "ntpSyncedAt": "20:14:08",
    "todoSyncStatus": "ok",
    "todoSyncedAt": "20:12:31",
    "lastError": ""
  }
}
```

## 4. 动作接口

### 4.1 页面导航动作

```ts
openSettings(): void
openAlarms(): void
openNetwork(): void
openPowerConfirm(): void
backHome(): void
requestShutdown(): void
```

说明：
- `enterMinimalMode` / `exitMinimalMode` 不建议作为外部导航动作暴露
- 极简模式切换应由 `presence` 与 `inactivity` 状态自动驱动
- `powerOn` 属于平台层唤醒行为，不属于页面导航动作

### 4.2 设置动作

```ts
setSoundMuted(value: boolean): Result
setAudioEnabled(value: boolean): Result
setVolume(level: number): Result
setEnvSampleInterval(seconds: number): Result
setVoiceFeatureEnabled(type: "environment" | "hourly" | "rest", value: boolean): Result
setVoiceRepeatCount(count: number): Result
```

### 4.3 闹钟动作

```ts
createAlarm(payload: AlarmPayload): Result
updateAlarm(id: string, payload: AlarmPayload): Result
deleteAlarm(id: string): Result
toggleAlarm(id: string, enabled: boolean): Result
```

```ts
interface AlarmPayload {
  hour: number
  minute: number
  second: number
  weekdays: number[]
  repeat: boolean
  enabled: boolean
}
```

### 4.4 网络动作

```ts
scanWifi(): Result
connectWifi(ssid: string, password: string): Result
disconnectWifi(): Result
syncTime(): Result
syncTodo(): Result
```

### 4.5 结果结构建议

```ts
interface Result {
  ok: boolean
  code?: string
  message?: string
}
```

建议失败时始终返回可读 `message`，用于网络页或语音提示区展示。

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
- `K3`：编辑
- `K4`：返回首页

编辑态：
- `K1`：减少 / 切换到上一个值
- `K2`：增加 / 切换到下一个值
- `K3`：确认当前项
- `K4`：取消本次编辑

### 5.3 闹钟页
列表态：
- `K1`：上移
- `K2`：下移
- `K3`：选择当前项
- `K4`：返回首页

动作态：
- `K1`：上移
- `K2`：下移
- `K3`：选择当前动作
- `K4`：返回列表态

编辑态：
- `K1`：减少当前字段
- `K2`：增加当前字段
- `K3`：确认当前字段并进入下一个字段
- `K4`：取消本次编辑

删除确认态：
- `K1`：取消删除
- `K4`：确认删除

### 5.4 网络页
模块选择态：
- `K1`：上一个模块
- `K2`：下一个模块
- `K3`：选择当前模块
- `K4`：返回首页

模块内选择态：
- `K1`：上移
- `K2`：下移
- `K3`：选择当前项
- `K4`：返回模块选择态

### 5.5 关机确认页
- `K1`：取消关机并返回首页
- `K4`：确认关机

## 6. 状态切换

### 6.1 人体检测触发
- 屏幕处于正常显示态时，只有连续无人达到 `30s` 且连续无操作达到 `30s`，才进入极简模式
- 屏幕处于极简模式时，雷达重新检测到人后立即返回首页
- 屏幕处于极简模式时，如果用户再次按键操作，也可立即返回首页
- 如果雷达状态异常，建议停用自动切换极简模式，维持正常显示
- 按键、点击、触摸不应被当作“有人”信号，但应刷新“无操作计时”

### 6.2 关机触发
- 首页 `K4` 进入关机确认页
- 确认后进入关机态
- 后续通过专门开机动作或按键/点击唤醒逻辑回到首页

## 7. 空数据与异常展示规则

### 7.1 时间未同步
- 首页日期旁显示 `UNSYNC`
- 仍显示本地运行时间

### 7.2 环境传感器异常
- 温度、湿度、光照显示 `--`
- 语音提示区可显示一条告警文案
- 保留布局，不因字段缺失而塌陷

### 7.3 无待办
- todo 区显示 `无待办`

### 7.4 无闹钟
- 最近闹钟区显示 `无闹钟`
- 闹钟管理页保留“新建闹钟”入口

### 7.5 WiFi 未连接
- 网络页明确显示 `未连接`
- NTP 与 todo 同步动作可以失败，但需返回可读错误原因

### 7.6 Todo 云同步失败
- 网络页显示失败原因
- 首页可以通过语音提示区展示轻量提醒

### 7.7 语音关闭
- `audioEnabled = false` 时，语音提示区仍可显示文本事件
- 但不要求实际播报

## 8. HTML 原型与 LVGL 对接建议

- HTML 中的区块已经按嵌入式页面结构拆分，可直接映射为 `LVGL` 容器
- 首页可拆为：时间区、状态区、环境条、语音条、闹钟卡、待办列表、按键栏
- 列表页可统一为：标题栏、列表区、详情区、按键栏
- 接口层建议保持“只读状态 + 动作命令”的单向流，不要让界面直接驱动底层状态对象

## 9. 当前原型文件

- `index.html`
- `assets/styles.css`
- `assets/app.js`

后续如转为 `LVGL`，建议先保留本文件中的：
- 页面命名
- 字段分组
- 按键语义
- 异常展示规则
