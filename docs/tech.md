# ESP32-S3 智能语音办公闹钟 ESP-IDF 技术实现设计 (tech.md)

## 1. 文档目标与冻结边界

本文基于当前目录下的 `feature.md`、`spec.md`、`pinmap.md`、`arch.md`，以及 `ESP-IDF` 官方组件接口资料，输出可直接指导编码、装配、联调与测试的 `ESP-IDF` 工程实现定义。

本阶段冻结以下内容：

- ESP-IDF 工程配置落点：`CMakeLists.txt`、`main/CMakeLists.txt`、`sdkconfig.defaults`、`partitions.csv`
- owner 模块边界、启动编排、执行上下文
- 数据交换机制、最新状态模型、命令路径、阻塞隔离方式
- 调试 / 日志 / Shell / ztest seam / fault injection seam

本阶段明确**不冻结**以下两类缺文档专题，只冻结机制边界，不进入业务细节：

- Todo 云端 HTTP 契约细节：URI、鉴权、GET/POST 语义分工、请求体 / 响应体 schema、分页 / 增量同步 / 幂等规则、冲突处理
- UI 设计细节：页面层级、widget 树、布局、视觉稿、导航交互细节、动画与文案

因此，`tech.md` 中与这两类专题相关的内容只会落到：

- 谁拥有该 capability
- 运行在哪个上下文
- 如何与其他模块交换数据
- 预留何种扩展 seam

不会越权冻结缺失文档中的业务协议或界面设计结论。

---

## 2. 参考加载清单

### 2.1 官方资料核对结果

本次实现直接以 `ESP-IDF` 组件接口为准，已对齐的主路径包括：

- `esp_lcd` + SPI 驱动 ST7789 屏
- `LEDC` 背光 PWM
- `GPIO ISR` 独立按键
- `I2C Master` 访问 BH1750
- `GPIO bit-bang` 访问 DHT11
- `UART` 接入 LD2410C
- `I2S` 驱动 MAX98357A
- `esp_wifi` + `esp_netif` + `lwIP`
- `SNTP`
- `NVS`

### 2.2 当前工程配置落点

- 顶层构建入口：`CMakeLists.txt`
- 组件构建入口：`main/CMakeLists.txt`
- 默认配置：`sdkconfig.defaults`
- 分区表：`partitions.csv`
- 应用源码：`src/`、`modules/`、`include/app/`

---

## 3. 输入追溯与能力绑定总览

### 3.1 项目实际出现的 capability

| Capability | Zephyr Provider | 最终 Owner |
| :--- | :--- | :--- |
| display capability | Display API + LVGL + `sitronix,st7789v` | `display_service` |
| input capability | Input Subsystem + `gpio-keys` | `input_service` |
| sensor capability | Sensor API + DHT11 + BH1750 | `environment_service` |
| presence transport capability | UART API + GPIO API + local LD2410C parser | `presence_service` |
| backlight capability | PWM API | `backlight_service` |
| network session capability | Wi-Fi Management + BSD sockets | `net_service` |
| time sync capability | SNTP library | `net_service` 内部 `time_sync_path` |
| remote fetch capability | HTTP client + BSD sockets | `net_service` 内部 `todo_sync_path` |
| persistence capability | Settings + NVS + Flash API | `persistence_broker` |
| audio output capability | I2S API | `audio_service` |
| logging capability | Logging Subsystem | 各 owner 模块 |
| shell capability | Shell Subsystem | 各 owner 模块的 command namespace |
| power policy capability | PM subsystem | `lifecycle_service` |

### 3.2 关键不可越界约束

- 显示、网络、音频、持久化必须各自唯一 owner
- 首次校时成功前，绝对时间提醒全部只可配置、不可激活
- UI 只消费公开快照，不直接抓底层设备
- todo 同步与时间同步必须通过同一网络 owner 串行裁决
- 不默认把所有状态广播为 zbus；只对真实需要的路径引入 IPC

---

## 4. 项目配置落地 (Project Configuration Landing)

## 4.1 工程目录建议

```text
app/
  boards/
    esp32s3_devkitc_procpu.overlay
    esp32s3_devkitc_procpu.conf
  src/
    main.c
    app_boot.c
    app_boot.h
    app_module.c
    app_module.h
  modules/
    core/
    config/
    sensing/
    interaction/
    connectivity/
    reminder/
    audio/
  include/app/
  Kconfig
  CMakeLists.txt
  prj.conf
```

目标板卡冻结为 `esp32s3_devkitc/procpu` 变体；若构建系统实际 target spelling 采用 Zephyr 新格式，则保持 `esp32s3_devkitc` + `procpu` 变体选择，不在业务代码里写死板级差异。

## 4.2 `boards/esp32s3_devkitc_procpu.overlay`

overlay 只表达硬件与启动时硬件事实，至少包含以下节点：

### 4.2.1 `/chosen`

- `zephyr,display = &lcd0`
- 保留 board DTS 中已有 `zephyr,console` / `zephyr,shell-uart` / `zephyr,flash` / `zephyr,code-partition`

### 4.2.2 TFT 显示

采用官方已存在的 `zephyr,mipi-dbi-spi` + `sitronix,st7789v` 路径：

- 选择一个空闲 SPI host 作为显示专用总线，优先 `spi2`
- `SCLK=GPIO12`、`MOSI=GPIO11`、`CS=GPIO10`
- `dc-gpios = GPIO4`
- `reset-gpios = GPIO8`
- `write-only`
- `mipi-mode = "MIPI_DBI_MODE_SPI_4WIRE"`
- 面板分辨率冻结为 `240x320`
- ST7789V 初始化参数放在 overlay，不落到 C 代码

说明：

- 具体 `pinctrl` 定义落在 board overlay / board conf 辅助片段中，不在业务模块自行 remap
- 页面 / widget 设计未冻结，但显示设备实例和 capability owner 已冻结

### 4.2.3 背光 PWM

- 选择一个 LEDC/PWM 通道绑定 `GPIO9`
- 作为 `backlight_service` 唯一受控输出
- 若板级没有现成背光设备 binding，则仅在 overlay 中声明 PWM 控制通道与 alias，不把亮度策略写进 DTS

### 4.2.4 环境传感器

- BH1750：挂在 `i2c0`，地址固定 `0x23`
- DHT11：节点 `compatible = "aosong,dht"`，`dio-gpios = <GPIO3>`
- DHT11 数据线外部上拉是硬件事实，记录于 `pinmap.md`，不再转成 Kconfig

### 4.2.5 LD2410C

- `uart1` 绑定 `GPIO17` / `GPIO18`
- `current-speed = <256000>`
- `GPIO16` 作为 OUT 状态输入
- 由于 Zephyr 无现成 LD2410C 业务 binding，本阶段只在 overlay 冻结 UART 实例与 OUT GPIO；协议解析保留为本地模块

### 4.2.6 I2S 音频输出

- 启用 `i2s0`
- `BCLK=GPIO13`、`WS/LRCK=GPIO14`、`DOUT=GPIO21`
- MAX98357A 不需要在 DTS 中建复杂 codec graph；本项目把 I2S controller 视为 `audio_service` 拥有的输出 capability

### 4.2.7 输入

- `gpio-keys` 建 4 个 child node
- 对应 `GPIO40`、`GPIO41`、`GPIO42`、`GPIO45`
- 均使用 `GPIO_PULL_UP | GPIO_ACTIVE_LOW`
- `debounce-interval-ms = 30`
- `zephyr,code` 冻结为 `INPUT_KEY_0..3`

### 4.2.8 NVS 分区

- 如 board 默认 `storage_partition` 不满足容量，需要在 overlay 中重定义或新增专用分区
- 该分区仅服务 Settings/NVS
- 本阶段不把云端 todo payload schema 落盘，因此分区尺寸按“系统参数 + 语音偏好 + 闹钟配置 + 少量元数据”估算

## 4.3 `boards/esp32s3_devkitc_procpu.conf`

此文件只承载板级构建差异，例如：

- PSRAM / flash 变体选择
- board 侧对 LVGL / Wi-Fi / I2S 栈空间的附加修正
- 若 ESP32-S3 PM / Wi-Fi 组合需要板级 fragment，则放这里

不把应用 feature 开关写进 board conf。

## 4.4 `prj.conf`

`prj.conf` 负责实例化本产品真正使用的 Zephyr 子系统：

### 基础

- `CONFIG_LOG=y`
- `CONFIG_SHELL=y`
- `CONFIG_INPUT=y`
- `CONFIG_PM=y`
- `CONFIG_FLASH=y`

### 驱动 / 子系统

- `CONFIG_GPIO=y`
- `CONFIG_I2C=y`
- `CONFIG_SPI=y`
- `CONFIG_UART=y`
- `CONFIG_PWM=y`
- `CONFIG_I2S=y`
- `CONFIG_SENSOR=y`
- `CONFIG_DISPLAY=y`
- `CONFIG_LVGL=y`

### 网络

- `CONFIG_NETWORKING=y`
- `CONFIG_NET_SOCKETS=y`
- `CONFIG_WIFI=y`
- `CONFIG_SNTP=y`
- `CONFIG_HTTP_CLIENT=y`
- `CONFIG_NET_TCP=y`
- `CONFIG_NET_UDP=y`
- `CONFIG_POSIX_API=y` 仅用于 socket / 时间转换辅助，不作为业务时间真源

### 存储

- `CONFIG_SETTINGS=y`
- `CONFIG_NVS=y`

### 调试

- `CONFIG_NET_SHELL=y`
- `CONFIG_LOG_MODE_DEFERRED=y`

### 应用级默认参数

以下参数不直接写固定字面值到业务代码，而是通过 `CONFIG_APP_*` 在 `Kconfig` 定义默认值，在 `prj.conf` 实例化：

- 环境采样周期
- 屏幕刷新合并窗口
- 语音仲裁队列深度
- 网络线程栈大小
- 音频线程栈大小
- 持久化提交防抖窗口
- 未同步时间占位刷新周期

## 4.5 `Kconfig`

应用级 Kconfig symbol 仅覆盖对产品变体有价值的项目：

- `CONFIG_APP_UI`
- `CONFIG_APP_AUDIO`
- `CONFIG_APP_TODO_SYNC`
- `CONFIG_APP_PM_LIGHT_SLEEP`
- `CONFIG_APP_ENV_SAMPLE_PERIOD_MS`
- `CONFIG_APP_UI_REFRESH_COALESCE_MS`
- `CONFIG_APP_SETTINGS_COMMIT_DELAY_MS`
- `CONFIG_APP_NET_THREAD_STACK_SIZE`
- `CONFIG_APP_AUDIO_THREAD_STACK_SIZE`
- `CONFIG_APP_PERSIST_THREAD_STACK_SIZE`
- `CONFIG_APP_NET_CMDQ_DEPTH`
- `CONFIG_APP_AUDIO_REQ_DEPTH`
- `CONFIG_APP_MAX_ALARMS`

禁止把 GPIO 编号、总线实例、设备地址这类固定硬件事实升成 Kconfig。

## 4.6 `CMakeLists.txt`

构建组织按功能域聚合：

- `src/main.c` 仅保留入口和 bootstrap
- `modules/<domain>/` 按 owner 模块纳入构建
- 通过 `if(CONFIG_APP_...)` 做裁剪
- 若后续引入 iterable section helper 或 linker snippet，仅由 CMake 接入

---

## 5. 系统编排与实例注册 (Orchestration & Registration)

## 5.1 注册 / 启动模式决策

### 候选方式

| 候选 | 结论 | 原因 |
| :--- | :--- | :--- |
| 全部业务模块仅靠 `main()` 手写初始化顺序 | 放弃 | 单实例虽固定，但 owner 较多，后续扩展和测试注入会让 `main()` 迅速膨胀 |
| 所有模块全靠 `SYS_INIT()` 完成初始化与启动 | 放弃 | 网络、存储、显示、音频存在阻塞与显式依赖，不适合在 `SYS_INIT()` 中完整启动 |
| `APP_MODULE_DEFINE` + iterable section 做注册，`SYS_INIT()` 仅做轻初始化，`main()` 承担显式启动 | 采用 | 符合 Zephyr 原生风格，同时保留启动依赖、阻塞路径和 shutdown 编排的显式可见性 |

### 最终模型

- 每个需要参与生命周期的 owner 模块定义一个 `app_module_desc`
- 通过 `STRUCT_SECTION_ITERABLE(app_module_desc, ...)` 注册
- `SYS_INIT(app_module_registry_init, APPLICATION, <prio>)` 只做：
  - kernel object 初始化
  - device handle 获取与 `device_is_ready()` 预检
  - callback 绑定
  - 静态快照默认值装载
- `main()` 调用 `app_boot_start()` 按依赖顺序显式启动线程 / delayable work / 定时器

## 5.2 `main.c` 的角色

`main.c` 只承担：

1. 调用模块注册表启动器
2. 触发 `persistence_broker` 首次加载
3. 启动 `lifecycle_service`
4. 等待 shutdown 请求
5. 在关机路径中调用统一 drain / stop 协调

`main.c` 不直接读传感器、不直接联网、不直接刷屏、不直接播放音频。

## 5.3 顶层可见对象

系统顶层允许可见的共享对象仅限：

- `app_module_desc` iterable section
- `lifecycle_service` 的全局 shutdown gate
- 若必须跨模块声明的只读配置常量

以下对象不得做成全局任意可见：

- 显示设备句柄
- socket / HTTP / SNTP session
- I2S 设备句柄
- NVS / Settings backend handle
- 语音队列 / 网络队列内部结构

## 5.4 启动顺序

启动顺序冻结为：

1. `fault_state`
2. `persistence_broker`
3. `settings_model`
4. `timebase_service`
5. `display_service`
6. `backlight_service`
7. `input_service`
8. `environment_service`
9. `presence_service`
10. `reminder_service`
11. `audio_service`
12. `net_service`
13. `lifecycle_service` 进入 steady-state

理由：

- 先有 fault / settings / timebase，后有用户可见输出
- 先起显示与输入，保证“未校时但可交互”的基础业务态
- 网络最后起，不阻塞 first frame

---

## 6. 模块设计 (Module Design)

## 6.1 目录与职责

| 模块路径 | 主要职责 | Owner 资源 | 执行上下文 |
| :--- | :--- | :--- | :--- |
| `modules/core/lifecycle_service.*` | 上电、降级、关机、light sleep 候选态裁决 | power policy | `main()` + 生命周期状态机 |
| `modules/core/fault_state.*` | fault summary 聚合与查询 | fault snapshot | 纯状态 owner，无独立线程 |
| `modules/core/timebase_service.*` | 时间有效门、epoch base、stale 判定 | `timebase_status` | 纯状态 owner，无独立线程 |
| `modules/config/settings_model.*` | 最新有效配置视图、校验、默认值 | `settings_snapshot` | 纯状态 owner，无独立线程 |
| `modules/config/persistence_broker.*` | Settings/NVS load/save/flush/reset | persistence capability | 专用线程 + `k_msgq` |
| `modules/interaction/input_service.*` | input callback、按键语义解释 | input capability | Input callback 上下文，快速提交 |
| `modules/interaction/ui_model.*` | page state、ui render model 聚合 | `ui_render_model` | 纯状态 owner + `k_mutex` |
| `modules/interaction/display_service.*` | LVGL / Display API 唯一 owner、刷新节流 | display capability | 专用线程 |
| `modules/interaction/backlight_service.*` | 亮度策略、PWM 应用 | backlight capability | `k_work_delayable` |
| `modules/sensing/environment_service.*` | DHT11/BH1750 周期采样、snapshot/stale/fault | sensor capability | 专用 `k_work_delayable` |
| `modules/sensing/presence_service.*` | LD2410 UART 字节流解析 + OUT GPIO 融合 | UART + GPIO | 专用线程 + `ring_buf` |
| `modules/connectivity/net_service.*` | Wi-Fi 连接、SNTP、todo sync 串行化 | network/session capability | 专用线程 + `k_msgq` |
| `modules/reminder/reminder_service.*` | 闹钟/报时/代办/休息提醒判定与 registry | reminder registry | `k_work_delayable` |
| `modules/audio/audio_service.*` | voice arbiter + I2S 播放 | audio output capability | 专用线程 + priority pending list |

## 6.2 模块边界细化

### `persistence_broker`

- 独占 `settings_load()` / `settings_save_one()` / backend flush
- 接收 `commit request`
- 对外只暴露：
  - `persistence_request_commit(reason_mask)`
  - `persistence_request_flush_sync(timeout)`
  - `persistence_get_last_result()`

### `display_service`

- 唯一持有 display device 与 LVGL 上下文
- 其他模块不得直接调用 LVGL API
- 只消费 `ui_render_model` 快照与显示模式命令
- 首帧策略：
  - settings 未加载完成时显示“基础业务态占位”
  - 时间未同步时显示无效占位，不阻塞主界面上线

### `environment_service`

- 唯一持有 DHT11 / BH1750 读取节奏
- 采用周期轮询，不使用 trigger 模式
- 失败时保留最近一次有效样本，并更新时间戳 / stale 标志

### `presence_service`

- OUT GPIO 只作为辅助状态，不直接驱动 UI / 背光
- UART 数据为主语义来源，OUT 为快速亮屏 hint 与一致性校验输入
- 使用本地协议解析器，不冒充 Zephyr 原生 LD2410 subsystem

### `net_service`

- Wi-Fi、DNS、SNTP、HTTP request 在同一 owner 线程内串行
- `time_sync_path` 与 `todo_sync_path` 是 `net_service` 内部子路径，不是两个独立 owner
- `remote sync policy` 在该线程内决定当前时间窗口优先执行校时还是 todo 同步
- 不直接冻结 todo REST 协议细节

### `audio_service`

- `voice arbiter` 与 `audio presenter` 放在同一 owner 模块中，以免音频请求与 I2S ownership 分裂
- 外部只提交 `voice_request`
- 模块内部完成优先级排序、重复抑制、播放与停止

### `ui_model`

- 仅聚合 arch 已定义的 `timebase_status`、`environment_snapshot`、`reminder_registry_view`、`fault_summary`、页面态
- 不冻结页面树、控件 ID 与视觉层实现

---

## 7. 数据交换设计 (Data Exchange Design)

## 7.1 latest-state / latest-view 路径

### 候选方式

| 候选 | 结论 | 放弃理由 |
| :--- | :--- | :--- |
| 所有状态统一上 zbus | 放弃 | 当前消费者集合固定，绝大多数只关心 latest state，不需要历史广播 |
| `k_msgq` 传状态更新 | 放弃 | 会堆积过期视图，不符合 UI / shell / policy 的 latest-state 语义 |
| owner snapshot + query API + dirty event | 采用 | 与公开交换契约最匹配，可控、可测、低耦合 |

### 最终实现

以下契约统一采用：

- owner 内部 `struct <xxx>_snapshot`
- `k_mutex` 保护写入与快照读取
- `uint32_t version` / `int64_t updated_at_ms` 做 freshness 判断
- 需要唤醒消费者时，通过轻量事件位或 direct kick 提醒，不传整份历史消息

适用对象：

- `system_readiness_state`
- `timebase_status`
- `environment_snapshot`
- `presence_status_view`
- `presence_session_view`
- `ui_render_model`
- `settings_snapshot`
- `reminder_registry_view`
- `todo_cache_view`
- `active_notification_state`
- `fault_summary`

## 7.2 command 路径

### 候选方式

| 场景 | 候选 | 结论 |
| :--- | :--- | :--- |
| 阻塞 owner 命令注入 | direct API | 放弃，调用点会暴露阻塞语义 |
| 阻塞 owner 命令注入 | `k_msgq` | 采用 |
| UI 局部状态编辑 | `k_msgq` | 放弃，状态机更新短小且无需排队线程 |
| UI 局部状态编辑 | direct owner API | 采用 |

### 最终命令路径

- `persistence_broker`：`k_msgq`
- `net_service`：`k_msgq`
- `audio_service`：`k_msgq`
- `display_service`：direct kick + dirty bit，不建 redraw msgq
- `ui_model` / `settings_model`：短小 direct API

### 背压 / 溢出策略

- 网络同步请求：
  - 同类重复请求合并成一个 pending bit
  - 若队列满，返回 `-EALREADY` 或 `-EAGAIN`，不复制多个相同同步任务
- 持久化提交：
  - 只保留 dirty reason mask，不积压多条相同 save request
- 语音请求：
  - 低优先级请求允许丢弃或覆盖
- 高优先级闹钟 / 代办提醒不可被更低优先级覆盖

## 7.3 raw byte stream 路径

LD2410 UART RX 采用：

- ISR / UART callback 仅搬运字节
- `ring_buf` 承载原始字节流
- `presence_service` 线程负责 framing / parse / state update

放弃 `k_msgq` 的原因：

- UART 帧长度可变
- 高频原始字节流不适合做固定小消息拷贝

## 7.4 fault 路径

### 候选方式

| 候选 | 结论 | 放弃理由 |
| :--- | :--- | :--- |
| 每个故障都广播 zbus | 放弃 | 当前主要消费者是 UI / shell / log，latest summary 足够 |
| owner 直接写日志，不留状态 | 放弃 | 不可测试，也不满足 arch 的 fault state 约束 |
| owner `fault_report()` -> `fault_state` latest summary | 采用 | 语义清晰，可被 UI / shell / ztest 断言 |

### 最终方式

- 各 owner 通过 `fault_state_report(module_id, fault_code, severity, recoverable)` 上报
- `fault_state` 更新 `fault_summary`
- UI 与 shell 只查 `fault_summary`
- Logging 记录状态跃迁，但不是跨域数据通道

## 7.5 settings / persistent schema

### 候选方式

| 候选 | 结论 | 放弃理由 |
| :--- | :--- | :--- |
| 业务模块直接用 NVS API | 放弃 | 破坏单一 write owner |
| Settings + NVS | 采用 | 已由 `spec.md` 冻结，且适合小型配置 |
| Settings + ZMS | 不采用 | 与 `spec.md` 已选 NVS 冲突，不在本阶段改写 |

### 逻辑 schema

冻结以下持久化类别：

- `system/`
  - 亮度
  - 音量
  - 静音
  - 环境采样周期
- `voice/`
  - 各类语音开关
  - 重复次数
- `alarm/`
  - 闹钟数组
  - 启用状态
  - 重复策略
- `meta/`
  - schema version

本阶段不冻结以下存储 schema：

- todo 云端 payload 的本地持久化结构
- UI 页面 / widget 恢复状态

### 写回策略

- UI 修改配置后先更新 `settings_model`
- `persistence_broker` 通过 `CONFIG_APP_SETTINGS_COMMIT_DELAY_MS` 防抖合并写回
- 用户主动关机时强制 flush
- 非法持久化值加载后回退默认值，并写 fault / warning

---

## 8. 执行上下文与竞争收敛

## 8.1 最终执行上下文选择

| 模块 / 路径 | 候选 | 采用方式 | 理由 |
| :--- | :--- | :--- | :--- |
| 环境采样 | thread / timer + work | `k_work_delayable` | 周期动作、阻塞较短、无需常驻阻塞线程 |
| 提醒判定 | thread / timer + work | `k_work_delayable` | 秒级或更粗粒度轮询，工作有界 |
| 背光策略应用 | direct / `k_work_delayable` | `k_work_delayable` | 需要合并 presence / settings / display mode 变化 |
| Wi-Fi + SNTP + HTTP | work / thread | 专用线程 | DNS / connect / recv / HTTP 均可能阻塞 |
| NVS 持久化 | work / thread | 专用线程 | flash 写入与 flush 有阻塞语义、需 shutdown 收敛 |
| I2S 播放 | work / thread | 专用线程 | 流式输出、buffer 生命周期、drain 需求明确 |
| LD2410 协议解析 | callback / work / thread | 专用线程 + ring buffer | 原始流解析与状态融合需要长期串行上下文 |
| LVGL / display | callback / work / thread | 专用线程 | LVGL API 必须单 owner 串行 |

## 8.2 共享资源竞争策略

| 资源 | 竞争来源 | 采用策略 |
| :--- | :--- | :--- |
| display capability | UI 刷新、提醒态、时间刷新、设置界面 | `display_service` 唯一 owner + latest render model |
| audio output capability | 问候、环境提示、报时、休息提醒、闹钟、代办 | `audio_service` 内部 arbiter 排序 |
| network session capability | Wi-Fi 连接、SNTP、todo 同步、shell 手动同步 | `net_service` 单线程串行 |
| persistence capability | 系统参数、语音偏好、闹钟配置、关机 flush | `persistence_broker` 单线程串行 |
| environment snapshot | sampler 写、UI/提醒读 | 单 writer snapshot + query |
| time validity gate | SNTP 写、UI/提醒读 | 单 writer snapshot + query |

---

## 9. 关键链路实现化

## 9.1 启动 -> 首帧 -> 首次校时

1. `SYS_INIT()` 完成模块轻初始化与设备 ready-check
2. `main()` 启动 `persistence_broker`
3. `persistence_broker` 加载 Settings，`settings_model` 生效
4. `display_service` 启动并显示基础业务态首帧
5. `environment_service` / `presence_service` 开始采样与会话跟踪
6. `net_service` 发起 Wi-Fi 连接
7. 首次 SNTP 成功后，`timebase_service` 设为 valid
8. `reminder_service` 收到 time-valid gate 开启，绝对时间提醒开始可激活

失败回退：

- Settings 加载失败：使用默认值，写 fault summary，系统继续进入基础业务态
- 显示 not-ready：标记 `display degraded`，允许其他业务继续，但 UI capability 视为 disabled
- 首次校时失败：保持 `timebase invalid`，不阻塞非绝对时间功能

## 9.2 环境采样 -> UI / 语音

1. `environment_service` 到期执行 `sensor_sample_fetch()` / `sensor_channel_get()`
2. 写入 `environment_snapshot`
3. 置 `ui dirty`，kick `display_service`
4. `reminder_service` 在下一周期读取最新快照判断是否需要环境语音提示

约束：

- 采样失败不清空最近有效值，只增加 stale / invalid 标志
- UI 刷新只读最新快照，不消费历史采样序列

## 9.3 在位检测 -> 背光 / 会话 / 休息提醒

1. UART callback 写入 `ring_buf`
2. `presence_service` 线程解析帧并读取 OUT GPIO 辅助校验
3. 形成 `presence_status_view`
4. 更新 `presence_session_view`
5. `backlight_service` 根据 presence + settings + display mode 合成目标亮度
6. `reminder_service` 读取会话累计时长判定休息提醒

## 9.4 语音仲裁 -> I2S 播放

1. `reminder_service` 或 `voice_policy` 生成 `voice_request`
2. `audio_service` 入队后按优先级 / 冷却窗口 / 重复策略排序
3. `audio_service` 选中当前条目，准备 PCM block 或 clip descriptor
4. I2S TX 开始播放
5. 播放完成后更新 `active_notification_state`

约束：

- `audio_service` 线程是 I2S 唯一 owner
- shell 不允许直接长时间驱动 I2S 播放流程，只能提交测试 clip request

## 9.5 配置编辑 -> 延迟持久化

1. `input_service` 将按键事件解释为菜单 / 修改意图
2. `ui_model` / `settings_model` 更新运行态配置
3. `backlight_service` / `reminder_service` / `audio_service` 消费最新设置
4. `persistence_broker` 收到合并后的 commit request
5. 防抖窗口到期后执行 save
6. 失败则回写 `fault_summary`，并保留运行态值与“未持久化”状态标记

## 9.6 安全关机 / light sleep 候选态

1. 用户关机命令进入 `lifecycle_service`
2. `lifecycle_service` 先阻止新网络同步 / 新语音 / 新持久化编辑请求
3. 请求 `audio_service` stop/drain
4. 请求 `persistence_broker` flush
5. 关闭背光，停止高频刷新
6. 断开或停止 Wi-Fi 活跃事务
7. 若平台 poweroff 路径经板级验证可用，则允许进入 `sys_poweroff()` 候选
8. 否则进入“业务停机 + 显示熄屏 + 可恢复待机”状态

说明：

- 本项目默认低功耗日常路径不是 deep sleep
- `sys_poweroff()` 不是默认必须路径，而是平台验证后的可选终态

---

## 10. Logging / Shell / 调试边界

## 10.1 Logging

- 各 owner 模块单独 `LOG_MODULE_REGISTER()`
- 多文件模块只允许一个 `.c` 做 `LOG_MODULE_REGISTER()`，其他文件 `LOG_MODULE_DECLARE()`
- 默认使用 deferred logging
- 记录重点：
  - 设备 ready / not-ready
  - 状态跃迁
  - fault summary 变更
  - 同步成功 / 失败
  - 配置加载 / 保存结果

## 10.2 Shell 命令树

只暴露 owner 语义，不暴露内部实现细节：

- `env show`
- `env sample`
- `presence show`
- `net state`
- `net sync-time`
- `net sync-todo`
- `settings show`
- `settings save`
- `alarm list`
- `audio test`
- `fault show`
- `power shutdown`

说明：

- `net sync-todo` 只触发同步请求，不冻结 HTTP payload 细节
- 不提供直接操作 LVGL widget、socket fd、NVS raw key、I2S 底层 buffer 的 shell 后门

---

## 11. 测试边界与 ztest 设计

## 11.1 module-level ztest seam

优先建设以下 fake capability：

- fake sensor provider
- fake storage backend
- fake network transport
- fake display sink
- fake I2S sink
- fake input source

可直接断言的 query / snapshot 面：

- `timebase_service_get_status()`
- `environment_service_get_snapshot()`
- `presence_service_get_status()`
- `settings_model_get_snapshot()`
- `fault_state_get_summary()`
- `audio_service_get_active_state()`
- `net_service_get_sync_state()`

## 11.2 integration ztest seam

至少覆盖：

1. 首次启动无有效时间 -> UI 占位 -> 校时成功后 gate 打开
2. 环境采样失败 -> 保留最近有效值 -> stale 标志更新
3. 配置修改 -> 合并提交 -> save fail -> fault summary 更新
4. todo 同步请求排队 -> 与时间同步冲突时被串行化
5. 关机路径 -> flush -> audio drain -> 进入待机 / poweroff 候选

## 11.3 fault injection seam

为关键 owner 设计显式一次性故障注入：

- 下一次 DHT11 读取失败
- 下一次 BH1750 读取超时
- 下一次 SNTP timeout
- 下一次 HTTP 请求失败
- 下一次 settings save fail
- 下一次 display refresh fail
- 下一次 I2S start fail

---

## 12. 最终收敛结论

### 12.1 已冻结的关键设计结论

- 采用 `Settings + NVS`，由 `persistence_broker` 单线程独占持久化
- 采用 `display_service` 单线程独占 `Display API + LVGL`
- 采用 `net_service` 单线程独占 Wi-Fi / sockets / SNTP / HTTP
- 采用 `audio_service` 单线程独占 I2S 与语音仲裁
- 大多数公开交换契约采用 snapshot + query API，不默认上 zbus
- LD2410 UART 原始流采用 `ring_buf` + parser thread
- 首次校时前用 `timebase_service` 做统一时间有效门控
- `SYS_INIT()` 只做轻初始化；完整启动与关机编排留在 `main()` / `lifecycle_service`

### 12.2 保留待补资料的未冻结项

- todo 云端 HTTP 业务协议细节
- UI 页面 / widget / 视觉 / 导航细节
- 若后续需要让 todo 缓存持久化，则必须先补云端数据 schema 文档后再扩展 `meta/` 之外的持久化结构

在不引入上述缺失文档的前提下，当前 `tech.md` 已足以指导下一阶段开始创建 Zephyr 工程骨架、overlay、Kconfig、模块文件与测试骨架。
