# ESP32-S3 智能语音办公闹钟 软件架构定义 (Software Architecture Definition)

## 一、领域视图 (Domain View)

### 1.1 系统生命周期域 (System Lifecycle Domain)

负责：定义系统启动、就绪、降级、关机、时间有效性和全局故障状态的稳定边界。

- lifecycle coordinator
  - 统筹上电、自检、进入基础业务态、进入完整业务态和安全关机。
- readiness state
  - 维护系统是否完成显示、配置、联网、时间基线建立等关键就绪条件。
- fault state
  - 维护关键外设故障、降级运行标志和对外可见的故障摘要。
- time validity gate
  - 维护“时间已同步/未同步”全局门控，限制依赖绝对时间的功能激活。

### 1.2 交互与显示域 (Interaction & Display Domain)

负责：统一处理按键输入、界面状态、显示输出和背光策略。

- input interpreter
  - 把 4 个独立按键输入解释为稳定的用户命令语义。
- ui state machine
  - 管理主界面、设置界面、闹钟设置界面、语音设置界面和提醒态的页面状态。
- ui model
  - 聚合时间、环境、闹钟、代办、故障和配置摘要，形成显示所需状态视图。
- display presenter
  - 独占显示 capability，统一发起屏幕刷新和模式切换。
- backlight policy
  - 根据用户亮度设置、在位状态和显示模式输出背光控制请求。

### 1.3 感知与在位域 (Sensing & Presence Domain)

负责：采集环境数据、解释人体存在状态，并维护连续在位会话。

- environment sampler
  - 编排 DHT11 和 BH1750 的周期采样与有效性判定。
- environment state
  - 维护最近一次有效环境快照和采样健康状态。
- presence interpreter
  - 融合 LD2410C 的 UART 数据与 OUT 引脚状态，形成稳定在位结果。
- presence session tracker
  - 维护在位会话、短暂离开宽限和连续停留累计时长。

### 1.4 联网与时间服务域 (Connectivity & Time Service Domain)

负责：管理 Wi-Fi 会话、网络校时和云端代办同步。

- wifi session controller
  - 管理 Wi-Fi 连接状态、重连窗口和联网可用性对外发布。
- time sync controller
  - 使用网络时间建立和修正系统时间基线。
- todo sync controller
  - 拉取云端代办事项并维护同步结果状态。
- remote sync policy
  - 裁决时间同步与代办同步对同一网络通道的使用时机。

### 1.5 提醒与音频通知域 (Reminder & Audio Notification Domain)

负责：管理闹钟、代办、环境建议、休息提醒和语音播放仲裁。

- reminder registry
  - 维护闹钟配置、代办提醒条件和播报策略的运行态视图。
- reminder evaluator
  - 在时间有效前提下判定闹钟、代办和休息提醒是否到达触发条件。
- voice policy
  - 根据用户偏好、冷却窗口和会话状态判断某类语音是否允许进入调度。
- voice arbiter
  - 维护统一语音请求优先级顺序，不直接暴露底层音频能力给其他域。
- audio presenter
  - 独占 I2S 音频输出 capability，负责短语音素材的实际输出。

### 1.6 配置与持久化域 (Configuration & Persistence Domain)

负责：维护可配置参数、运行期设置提交和本地非易失存储边界。

- settings model
  - 维护亮度、音量、静音、采样周期、语音偏好、闹钟配置等最新有效配置。
- persistence broker
  - 独占 NVS 持久化 capability，统一执行配置加载和保存。
- config commit policy
  - 约束多类设置的提交时机，避免跨域直接并发写存储。

## 二、能力依赖视图 (Capability Dependency View)

| Capability | 提供来源 (Provider) | 使用域 (Consumer Domain) | 说明 |
| :--- | :--- | :--- | :--- |
| kernel timing capability | FreeRTOS Tick / esp_timer / time.h | 系统生命周期域、感知与在位域、提醒与音频通知域 | 提供周期触发、会话累计、冷却窗口和绝对时间相关判定基础。 |
| display capability | SPI Master / `esp_lcd` / LVGL | 交互与显示域 | 提供统一显示输出和 GUI 渲染能力。 |
| input capability | GPIO ISR / FreeRTOS queue | 交互与显示域 | 提供按键事件分发、去抖和输入语义承载。 |
| sensor capability | GPIO / I2C Master / local DHT11 + BH1750 access path | 感知与在位域 | 提供环境数据读取能力。 |
| presence transport capability | UART Driver / GPIO Driver / local LD2410C decoder | 感知与在位域 | 提供雷达状态采集与协议解释能力。 |
| backlight capability | LEDC PWM / local backlight control path | 交互与显示域 | 提供屏幕背光亮度和息屏控制能力。 |
| network session capability | esp_wifi / esp_netif / lwIP Sockets | 联网与时间服务域 | 提供联网连接、可达性和 socket 传输能力。 |
| time sync capability | SNTP / time.h | 联网与时间服务域 | 提供网络校时能力。 |
| remote fetch capability | HTTP Client / lwIP Sockets | 联网与时间服务域 | 提供云端代办拉取能力。 |
| persistence capability | NVS / Flash API | 配置与持久化域 | 提供本地配置和业务状态持久化能力。 |
| audio output capability | I2S Driver / MAX98357A | 提醒与音频通知域 | 提供数字音频播放能力。 |
| logging capability | ESP Logging | 全部域 | 提供模块级观测、故障记录和诊断输出能力。 |
| shell capability | Console / idf.py monitor | 系统生命周期域、感知与在位域、联网与时间服务域、配置与持久化域 | 提供状态查询、调试命令和诊断入口。 |
| power policy capability | ESP-IDF PM | 系统生命周期域、交互与显示域 | 提供 light sleep 候选态和设备电源状态管理能力。 |

## 三、运行时协作契约 (Runtime Collaboration Contracts)

### 3.1 启动、时间基线与系统就绪

| 源 (Source) | 目标 (Target) | 协作语义 (Collaboration Semantic) | 交互内容 (Interaction Content) |
| :--- | :--- | :--- | :--- |
| persistence broker | settings model | data | 启动阶段加载的最新有效配置 |
| settings model | lifecycle coordinator | state | 启动所需默认参数与功能开关状态 |
| wifi session controller | time sync controller | trigger | 网络可用后允许开始校时 |
| time sync controller | time validity gate | state | 时间基线是否已建立 |
| time validity gate | reminder evaluator | state | 绝对时间功能可否激活 |
| fault state | ui model | state | 当前故障摘要和降级状态 |

### 3.2 环境采样与界面显示

| 源 (Source) | 目标 (Target) | 协作语义 (Collaboration Semantic) | 交互内容 (Interaction Content) |
| :--- | :--- | :--- | :--- |
| environment sampler | environment state | data | 最近一次环境样本与采样健康结果 |
| environment state | ui model | state | 时间、温湿度、光照所需的环境快照 |
| reminder registry | ui model | state | 闹钟摘要、代办摘要和提醒状态 |
| ui state machine | ui model | command | 当前页面模式和显示焦点切换 |
| ui model | display presenter | state | 屏幕需要呈现的统一视图 |
| backlight policy | display presenter | command | 亮屏、极简显示、息屏对应的显示输出模式 |
| backlight policy | backlight capability | ownership | 使用背光能力完成亮度和开关控制 |

### 3.3 在位检测、低功耗与会话跟踪

| 源 (Source) | 目标 (Target) | 协作语义 (Collaboration Semantic) | 交互内容 (Interaction Content) |
| :--- | :--- | :--- | :--- |
| presence transport capability | presence interpreter | data | 雷达串口结果和 OUT 状态 |
| presence interpreter | presence session tracker | state | 稳定在位/离位状态 |
| presence interpreter | backlight policy | trigger | 亮屏或息屏策略切换请求 |
| presence session tracker | voice policy | state | 当前会话是否已问候、连续在位时长 |
| presence session tracker | reminder evaluator | state | 休息提醒累计条件 |
| lifecycle coordinator | power policy capability | ownership | 系统可进入的低功耗候选态边界 |

### 3.4 联网、校时与代办同步

| 源 (Source) | 目标 (Target) | 协作语义 (Collaboration Semantic) | 交互内容 (Interaction Content) |
| :--- | :--- | :--- | :--- |
| remote sync policy | wifi session controller | query | 当前联网通道是否可分配给校时或代办同步 |
| wifi session controller | todo sync controller | trigger | 云端同步可执行窗口 |
| todo sync controller | reminder registry | data | 最新代办缓存及提醒属性 |
| todo sync controller | ui model | state | 代办同步状态和代办摘要刷新请求 |
| time sync controller | ui model | state | 时间同步状态和当前时间基线摘要 |
| todo sync controller | fault state | fault | 同步失败、数据格式异常等网络侧故障摘要 |

### 3.5 提醒生成与语音播报

| 源 (Source) | 目标 (Target) | 协作语义 (Collaboration Semantic) | 交互内容 (Interaction Content) |
| :--- | :--- | :--- | :--- |
| environment state | reminder evaluator | state | 环境异常判断所需快照 |
| time validity gate | reminder evaluator | state | 当前是否允许绝对时间提醒 |
| reminder registry | reminder evaluator | state | 闹钟、代办提醒配置 |
| reminder evaluator | voice policy | trigger | 某类提醒到达候选触发条件 |
| voice policy | voice arbiter | command | 允许进入调度的语音请求 |
| voice arbiter | audio presenter | command | 按优先级下发的可播报语音项目 |
| audio presenter | audio output capability | ownership | 使用 I2S 音频能力完成播放 |
| voice arbiter | ui model | state | 当前播报中或待播报提醒摘要 |

### 3.6 配置修改与持久化

| 源 (Source) | 目标 (Target) | 协作语义 (Collaboration Semantic) | 交互内容 (Interaction Content) |
| :--- | :--- | :--- | :--- |
| input interpreter | ui state machine | command | 菜单导航、确认、返回和参数修改意图 |
| ui state machine | settings model | command | 对亮度、音量、采样周期、语音偏好的编辑请求 |
| settings model | persistence broker | command | 需要提交的最新有效配置 |
| persistence broker | settings model | state | 保存成功、保存失败或回滚结果 |
| settings model | backlight policy | state | 最新亮度策略 |
| settings model | voice policy | state | 语音启停、音量和重复次数策略 |
| settings model | environment sampler | state | 最新采样周期配置 |
| settings model | reminder registry | state | 最新闹钟配置和提醒启停策略 |

## 四、公开交换契约 (Public Exchange Contracts)

| 名称 (Name) | 语义 (Semantic) | 生命周期 (Lifecycle) | 生产者 (Producer) | 消费者 (Consumer) | 拥有者 (Owner) |
| :--- | :--- | :--- | :--- | :--- | :--- |
| system_readiness_state | 当前系统处于基础业务态、完整业务态、降级态还是关机收尾态 | 最新有效 | lifecycle coordinator | ui model、reminder evaluator、wifi session controller | readiness state |
| timebase_status | 当前系统时间是否有效、最近一次校时结果和时间基线健康状态 | 最新有效 | time sync controller | ui model、reminder evaluator、fault state | time validity gate |
| environment_snapshot | 最近一次有效温度、湿度、光照采样结果及采样时间戳 | 最新有效 | environment sampler | ui model、reminder evaluator、shell capability | environment state |
| presence_status_view | 当前稳定在位/离位状态及其可信度摘要 | 最新有效 | presence interpreter | backlight policy、presence session tracker、shell capability | presence interpreter |
| presence_session_view | 当前会话是否已问候、累计在位时长和离开宽限状态 | 会话期 | presence session tracker | voice policy、reminder evaluator | presence session tracker |
| ui_render_model | 当前页面所需的完整显示视图 | 最新有效 | ui model | display presenter | ui model |
| settings_snapshot | 用户配置的最新有效视图，包括亮度、音量、采样周期、语音偏好和闹钟配置 | 最新有效 | settings model | backlight policy、voice policy、environment sampler、reminder registry、shell capability | settings model |
| reminder_registry_view | 当前有效闹钟、代办提醒及其启停状态 | 最新有效 | reminder registry | reminder evaluator、ui model | reminder registry |
| todo_cache_view | 最近一次成功同步的代办缓存和同步状态摘要 | 最新有效 | todo sync controller | reminder registry、ui model | reminder registry |
| voice_request | 某条待播报语音请求及其业务语义 | 瞬时 | voice policy | voice arbiter | 无长期拥有者 |
| active_notification_state | 当前播报中、排队中或待用户处理的提醒摘要 | 最新有效 | voice arbiter | ui model、shell capability | voice arbiter |
| fault_summary | 当前关键故障、降级原因和恢复建议摘要 | 最新有效 | fault state | ui model、shell capability、logging capability | fault state |

## 五、激活依赖 (Activation Dependencies)

| 单元 (Unit) | Requires | After | Before | Failure Effect |
| :--- | :--- | :--- | :--- | :--- |
| persistence broker | persistence capability | flash ready | settings model active | disabled |
| settings model | persistence broker | persisted settings loaded | ui state machine active, reminder registry active | degraded |
| lifecycle coordinator | logging capability, settings model | basic platform ready | full business activation | degraded |
| display presenter | display capability | display device ready | ui visible output | disabled |
| backlight policy | backlight capability, settings model | display presenter ready | visible mode switch | degraded |
| environment sampler | sensor capability, kernel timing capability | settings model active | environment state active | degraded |
| presence interpreter | presence transport capability | basic platform ready | presence session tracker active | degraded |
| presence session tracker | presence interpreter, kernel timing capability | stable presence available | rest reminder eligibility | degraded |
| wifi session controller | network session capability | basic platform ready | time sync controller active, todo sync controller active | degraded |
| time sync controller | wifi session controller, time sync capability | network available | absolute-time reminder activation | degraded |
| todo sync controller | wifi session controller, remote fetch capability | remote sync policy active | reminder registry complete | optional |
| reminder registry | settings model | settings ready | reminder evaluator active | degraded |
| reminder evaluator | timebase_status, reminder_registry_view, kernel timing capability | reminder registry active | voice policy requests | degraded |
| voice policy | settings_snapshot, presence_session_view | reminder evaluator active | voice arbiter active | degraded |
| voice arbiter | voice_request, logging capability | voice policy active | audio presenter playback | degraded |
| audio presenter | audio output capability | voice arbiter active | audible notification output | disabled |
| input interpreter | input capability | basic platform ready | ui state machine navigation | degraded |
| ui state machine | input interpreter, settings model | display presenter ready | ui model updates | degraded |
| ui model | environment_snapshot, timebase_status, reminder_registry_view, fault_summary | core states available | display presenter output | degraded |

## 六、行为约束 (Behavioral Constraints)

### 6.1 全局约束 (Global Constraints)

- 不得把 `feature.md` 中的单个功能直接映射为单个线程、单个任务或单个 work item。
- 显示、音频、网络、持久化这四类 capability 必须各自有明确归属单元，其他领域不得无边界直接调用。
- 在首次校时成功前，所有依赖绝对时间的功能只能保持“可配置但未激活”状态。
- 日常低功耗路径限定为亮屏业务态与息屏/极简显示 + light sleep 候选态，不把 deep sleep 作为默认运行路径。
- 不得在架构层写死线程优先级、栈大小、队列深度、缓冲区大小和具体 IPC 原语。

### 6.2 系统生命周期域约束

- lifecycle coordinator 负责定义完整业务态与降级态边界，但不直接承担各具体外设的业务处理。
- time validity gate 必须作为绝对时间功能的统一激活门，不允许各业务域自行定义“时间是否有效”。
- fault state 维护系统级故障摘要，其他域只能上报 fault，不得绕过其自行向多个消费者分发故障定义。

### 6.3 交互与显示域约束

- display capability 只能由 display presenter 统一占有和使用。
- ui model 只消费公开交换契约，不直接读取底层驱动或硬件寄存器状态。
- backlight policy 只输出背光控制语义，不承担在位判断或用户配置解析职责。
- 输入语义必须先经过 input interpreter，再进入 ui state machine；业务域不得直接把 GPIO 读值当作命令。

### 6.4 感知与在位域约束

- environment sampler 负责采样编排与有效性判定，其他域只消费 environment_snapshot。
- presence interpreter 是在位状态的唯一真源，不能由 UI、提醒或音频域各自维护独立在位判断。
- presence session tracker 只维护会话连续性与累计时长，不直接触发播报或页面跳转。

### 6.5 联网与时间服务域约束

- Wi-Fi 会话、网络校时和云端代办同步必须通过 remote sync policy 协调，不允许多个单元直接争用同一网络窗口。
- time sync controller 只负责建立和修正时间基线，不直接管理闹钟或代办业务规则。
- todo sync controller 只维护远端代办同步结果和本地缓存，不直接决定播报优先级。

### 6.6 提醒与音频通知域约束

- reminder evaluator 负责决定“是否到达触发条件”，不直接拥有音频输出能力。
- voice policy 负责冷却窗口、用户偏好和会话门控，不直接操作 I2S。
- voice arbiter 是语音请求进入音频链路前的唯一仲裁入口。
- audio presenter 只负责播放，不反向决定播报优先级、业务语义或冷却策略。

### 6.7 配置与持久化域约束

- persistence capability 只能由 persistence broker 统一使用。
- settings model 是用户配置的唯一最新有效视图，其他域不得维护私有持久化副本作为业务真源。
- 闹钟配置、系统参数和语音偏好对存储的提交必须通过 config commit policy 串行化，不允许跨域并发写入 NVS/Settings。

### 6.8 调试与观测约束 (Debug & Observability Constraints)

- Shell 入口只能查询或注入公开语义，例如查看环境快照、时间同步状态、当前故障摘要和同步结果；不得绕过领域边界直接操作底层业务内部对象。
- Logging 由各域模块分别输出，但日志语义应围绕 capability 使用结果、状态跃迁和 fault 摘要，不把日志当作跨域数据通道。
- 调试命令允许触发手动采样、手动校时、手动代办同步和状态查询，但这些请求必须先进入对应领域的公开 command 语义。
- 不得通过调试入口直接抢占 display capability、audio output capability、network session capability 或 persistence capability。 
