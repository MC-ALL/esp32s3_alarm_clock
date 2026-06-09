# 智能闹钟 UI 设计文档

## 1. 文档范围

- 屏幕规格：`240x320` 竖屏
- 本文档只描述当前固件中真实存在的页面、视图和交互
- 以 `modules/interaction/ui_model.c` 的实现为准

---

## 2. 页面总览

当前 UI 主页面共有 6 个：

1. `HOME`
2. `ALARM`
3. `TODO`
4. `ENV`
5. `WIFI`
6. `LOW_CLOCK`

其中：

- `LOW_CLOCK` 是低干扰时钟页，会在满足离开条件后自动进入
- 其余页面都可以通过 4 个按键进行切换

除主页面外，当前还存在以下设置或编辑视图：

- `HOME_SETTINGS`
- `ALARM_SETTINGS`
- `ALARM_ITEM`
- `TODO_SETTINGS`
- `TODO_ITEM`
- `TODO_DELETE_CONFIRM`
- `ENV_SETTINGS`
- `LOW_SETTINGS`

---

## 3. 通用交互规则

设备共有 4 个实体按键，底栏以图标方式显示当前语义。

在主页面状态下：

- 第 1 个按键：返回首页，或在列表页中向下切换焦点
- 第 2 个按键：切换到上一个主页面
- 第 3 个按键：切换到下一个主页面
- 第 4 个按键：进入当前页面对应的设置或操作视图

在设置或编辑视图下：

- 第 1 个按键：返回上一级
- 第 2 个按键：上移焦点
- 第 3 个按键：下移焦点
- 第 4 个按键：执行当前焦点动作

---

## 4. 首页 `HOME`

### 4.1 页面作用

首页用于集中显示当前最重要的信息。

### 4.2 显示内容

当前首页包含 4 个主要区域：

- 顶部时间区
  - 当前时间
  - 日期
  - 12/24 小时制状态
- 左下闹钟区
  - 最近一个已启用闹钟时间
- 右下 Todo 区
  - 当前未完成 Todo 数量
- 底部按键栏

当 `home_clock_only=true` 时，首页会切换成“纯时钟版”，只保留时间与日期。

### 4.3 主页面切换

- 第 1 键：若当前不在首页则返回首页
- 第 2 键：切换到前一个主页面
- 第 3 键：切换到后一个主页面
- 第 4 键：进入 `HOME_SETTINGS`

### 4.4 设置页 `HOME_SETTINGS`

当前设置项：

1. `LAYOUT CLOCK / LAYOUT FULL`
2. `FORMAT 24H / FORMAT 12H`
3. `HOUR TONE ON / OFF`

说明：

- 第 4 键直接切换当前项
- 第 1 键返回主页面

---

## 5. 闹钟页 `ALARM`

### 5.1 页面作用

闹钟页用于浏览现有闹钟列表。

### 5.2 显示内容

页面标题为 `ALARM`，下方显示所有本地闹钟：

- 时间
- 启用状态 `ON / OFF`

### 5.3 主页面交互

在 `ALARM` 主页面下：

- 第 1 键：列表焦点下移
- 第 2 键：切换到前一个主页面
- 第 3 键：切换到后一个主页面
- 第 4 键：进入 `ALARM_SETTINGS`

### 5.4 设置页 `ALARM_SETTINGS`

当前设置项：

1. `VOICE ON / OFF`
2. `TEST VOICE`
3. `ADD ALARM`
4. `DELETE LAST`
5. 现有闹钟列表

### 5.5 闹钟项编辑页 `ALARM_ITEM`

当前可编辑字段：

1. 时间
2. `REPEAT`
3. `VOICE`
4. `ENABLE`

说明：

- 第 4 键执行当前字段动作
- 时间以 `5` 分钟为步进递增
- 修改后立即保存到 `settings_model`

---

## 6. Todo 页 `TODO`

### 6.1 页面作用

Todo 页用于查看当前同步到本地缓存的待办事项。

### 6.2 显示内容

页面标题为 `TODO`，主体显示：

- 未完成或已完成 Todo 列表
- 每条 Todo 的完成状态图标
- 末尾一个 `SETTINGS` 入口

当当前没有 Todo 且同步正在进行时，会显示 `SYNCING`。

### 6.3 主页面交互

在 `TODO` 主页面下：

- 第 1 键：列表焦点下移
- 第 2 键：切换到前一个主页面
- 第 3 键：切换到后一个主页面
- 第 4 键：
  - 如果焦点在 Todo 项上，进入 `TODO_ITEM`
  - 如果焦点在 `SETTINGS` 上，进入 `TODO_SETTINGS`

### 6.4 设置页 `TODO_SETTINGS`

当前设置项：

1. `SYNC NOW`
2. `REFRESH xM`
3. `VOICE ON / OFF`
4. `TEST VOICE`

说明：

- `SYNC NOW` 会手动触发一次 Todo 同步
- `REFRESH` 会切换自动刷新周期

### 6.5 Todo 项视图 `TODO_ITEM`

当前操作项：

1. `MARK DONE / UNDO DONE`
2. `DELETE`

### 6.6 Todo 删除确认页 `TODO_DELETE_CONFIRM`

当前操作项：

1. `CONFIRM DELETE`
2. `CANCEL`

---

## 7. 环境页 `ENV`

### 7.1 页面作用

环境页用于集中展示温度、湿度、光照和人体存在状态。

### 7.2 显示内容

当前页面分为 4 个色块区域：

- `TEMP`
- `HUMI`
- `LUX`
- `RADAR`

当环境数据超过当前设置阈值时，对应区域会变为告警色。

### 7.3 主页面交互

在 `ENV` 主页面下：

- 第 2 键：切换到前一个主页面
- 第 3 键：切换到后一个主页面
- 第 4 键：进入 `ENV_SETTINGS`

### 7.4 设置页 `ENV_SETTINGS`

当前设置项包括：

1. `VOICE ON / OFF`
2. `TEST VOICE`
3. `SAMPLE`
4. `T LOW`
5. `T HIGH`
6. `H LOW`
7. `H HIGH`
8. `L LOW`
9. `L HIGH`
10. `ALERT ON / OFF`

说明：

- 第 4 键执行当前项动作
- 阈值修改后立即保存

---

## 8. 网络页 `WIFI`

### 8.1 页面作用

网络页用于查看当前网络、时间同步和 Todo 同步状态。

### 8.2 显示内容

当前页面由 4 个信息块组成：

- `WIFI`
  - 显示 `ON / OFF`
- `IP`
  - 显示当前 IP 地址
- `TIME`
  - 显示 `SYNC / WAIT`
- `TODO`
  - 显示 `OK / ERR / ...`

### 8.3 主页面交互

在 `WIFI` 主页面下：

- 第 2 键：切换到前一个主页面
- 第 3 键：切换到后一个主页面
- 第 4 键：当前无独立设置页，不进入新的视图

说明：

- 当前 `WIFI` 页面本身不再使用旧设计中的 `WIFI STATUS / ACTION / MY NET / SYNC STATUS` 四模块结构
- `CONNECT NOW` 和 `SYNC TODO` 这类操作不在当前主页面上直接显示为独立子模块

---

## 9. 低干扰时钟页 `LOW_CLOCK`

### 9.1 页面作用

`LOW_CLOCK` 用于在长时间无人时显示简化版时钟。

### 9.2 进入条件

当前逻辑中，当以下条件满足时会进入该页：

- 持续无人一段时间
- 当前处于主页面状态

进入阈值由设置项 `low_enter_absent_s` 控制。

### 9.3 退出条件

当重新检测到人在位，并持续满足退出时间阈值后，系统会返回首页。

退出阈值由设置项 `low_exit_present_s` 控制。

### 9.4 显示内容

页面仅显示：

- 时间
- 日期
- 12/24 小时制下的时间样式

### 9.5 设置页 `LOW_SETTINGS`

当前设置项：

1. `FORMAT 24H / FORMAT 12H`
2. `ENTER xS`
3. `EXIT xS`

---

## 10. 当前实现和旧方案的差异说明

为避免误解，当前代码和早期设计相比已有以下明显变化：

1. 当前主页面已经不是旧的 `HOME / SETTINGS / NETWORK / POWER` 结构。
2. 当前存在独立的 `TODO` 页面和 `ENV` 页面。
3. 低功耗显示主线已收敛为 `LOW_CLOCK` 页面，而不是文档中旧的 `MINIMAL` 独立模式描述。
4. 当前没有单独的普通关机确认页交互主线。
5. Todo 页面不只是展示，还支持：
   - 手动同步
   - 标记完成
   - 删除
6. 音频、网络、Todo 和设置逻辑已经进入实际可运行状态，而不再只是占位。
