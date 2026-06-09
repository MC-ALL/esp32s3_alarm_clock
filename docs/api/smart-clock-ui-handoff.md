# 智能闹钟界面接口交接文档

## 1. 文档目标

本文档面向后续继续维护当前固件页面、动作接口和 Web 控制中心对接逻辑的同学。

目标是明确：
- 当前页面真实有哪些页面和视图
- 当前页面读取哪些运行状态
- 当前页面会触发哪些动作
- 当前哪些动作已经直接对接到 Web 控制中心

## 2. 当前 UI 页面集合

当前主页面为：
- `HOME`
- `ALARM`
- `TODO`
- `ENV`
- `WIFI`
- `LOW_CLOCK`

当前附属视图为：
- `HOME_SETTINGS`
- `ALARM_SETTINGS`
- `ALARM_ITEM`
- `TODO_SETTINGS`
- `TODO_ITEM`
- `TODO_DELETE_CONFIRM`
- `ENV_SETTINGS`
- `LOW_SETTINGS`

## 3. 页面状态与字段

### 3.1 首页 `HOME`

主要读取：
- 当前时间
- 当前日期
- 最近一个已启用闹钟
- 未完成 Todo 数量
- 首页布局模式

### 3.2 闹钟页 `ALARM`

主要读取：
- 闹钟列表
- 每条闹钟的时间
- 是否重复
- 是否启用
- 是否启用语音

当前页面支持：
- 浏览闹钟列表
- 新增闹钟
- 删除最后一个闹钟
- 编辑单条闹钟

### 3.3 Todo 页 `TODO`

主要读取：
- 当前未完成 Todo 列表
- Todo 同步状态
- Todo 提示语音开关

当前页面支持：
- 查看 Todo 列表
- 手动同步
- 标记完成
- 删除 Todo

当前语义：
- 设备完成 Todo 后，Web 端归档该项
- 设备删除 Todo 后，Web 端彻底删除该项
- 设备本地后续不再保留已完成 Todo

### 3.4 环境页 `ENV`

主要读取：
- 温度
- 湿度
- 光照
- 雷达状态
- 环境提醒阈值
- 环境语音开关

### 3.5 网络页 `WIFI`

主要读取：
- WiFi 是否连接
- 当前 IP
- 时间是否已同步
- Todo 同步是否成功

说明：
- 当前 `WIFI` 页面以状态展示为主
- 当前设备状态页仍在 Web 端，不在设备端展示

### 3.6 低干扰时钟页 `LOW_CLOCK`

主要读取：
- 当前时间
- 当前日期
- 12/24 小时制
- 人体在位状态
- 进入和退出阈值

## 4. 关键动作接口

### 4.1 闹钟相关

当前本地行为：
- 新增闹钟
- 删除最后一个闹钟
- 修改单条闹钟时间与属性
- 启停闹钟

当前新增对接：
- 本地保存成功后，会立即请求 Web 更新闹钟真源

### 4.2 语音设置相关

当前本地行为：
- Todo 提示语音开关
- 闹钟语音开关
- 环境语音开关
- 环境提醒开关
- 整点提示开关

当前新增对接：
- 本地保存成功后，会立即请求 Web 更新语音设置真源

### 4.3 Todo 相关

当前真实网络动作：
- `net_service_request_todo_sync_now()`
- `net_service_request_todo_set_done()`
- `net_service_request_todo_delete()`

当前语义：
- Todo 完成会立即请求 Web 归档
- Todo 删除会立即请求 Web 删除

### 4.4 状态与事件相关

当前真实对接还包括：
- 周期拉取 `GET /api/device/config`
- 周期上报 `POST /api/device/status`
- 事件上报 `POST /api/device/events`

设备当前会上报的关键事件包括：
- `alarm_triggered`
- `env_alert_triggered`
- `rest_reminder_triggered`
- `todo_sync_up_played`
- `todo_completed`
- `todo_deleted`
- `welcome_played`

## 5. 当前按键语义

### 5.1 主页面状态

在主页面状态下：
- 第 1 键：返回首页，或在列表页中向下移动焦点
- 第 2 键：切换到前一个主页面
- 第 3 键：切换到后一个主页面
- 第 4 键：进入当前页面对应设置或操作视图

### 5.2 设置或编辑视图

在设置或编辑视图下：
- 第 1 键：返回上一级
- 第 2 键：上移焦点
- 第 3 键：下移焦点
- 第 4 键：执行当前项动作

## 6. 当前实现限制

1. 当前设备端不展示已完成 Todo。
2. 当前设备端不存储事件历史，也不查看事件历史。
3. 当前配置快照解析仍是轻量实现。
4. 更完整的控制中心扩展设计，见 `docs/plans/2026-06-09-web-device-control-center-design.md`。
