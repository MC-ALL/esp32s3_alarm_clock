# ESP32 智能语音办公闹钟 功能定义文档 (Feature Definition Document)

## 平台约束 (Platform Constraints)

1. 本文档默认软件平台为 `ESP-IDF on ESP32-S3`，系统时间依赖联网后的首次校时建立有效时间基线，不假定芯片内置 RTC 具备断电后持续保时能力。
2. 所有基于绝对时间的功能，包括时间显示、闹钟触发和代办提醒，在首次校时成功前仅允许配置，不允许激活触发。
3. 本文档将低功耗待机的默认策略限定为“亮屏业务态”和“息屏/极简显示 + light sleep 候选态”两级，不默认把 deep sleep 作为日常待机路径。
4. 若后续规格要求引入 deep sleep、掉电后继续走时或统一 RTC 标准 API，应在 `spec` 阶段显式补充外置 RTC、唤醒源和时间恢复机制。

## 模块类别 (Module Category): 系统与电源管理

### Feature (功能): F01_PowerOnAndShutdown
**功能概述 (Feature Overview):** 设备在上电后完成初始化并进入可交互工作态；若尚未完成首次校时，则仅进入不依赖绝对时间的基础业务态，用户可通过按键发起安全关机。

| 触发类型 (Trigger Type) | 触发条件描述 (Trigger Description) | 预期响应 (Expected Response) | 例外处理 (Exception Handling) |
| :--- | :--- | :--- | :--- |
| `硬件事件 (Hardware Event)` | Type-C 5V 供电接入，系统上电 | **前置状态 (Pre-condition):** `POWER_OFF`<br>**动作 (Action):** 初始化 ESP32-S3、显示屏、RTC Timer/系统时基、WiFi、传感器、雷达与音频通道，加载本地配置后进入正常工作状态。 | **条件 (Condition):** 关键外设初始化失败、配置加载失败，或当前尚未建立有效系统时间基线<br>**动作 (Action):** 记录故障或未校时标志，允许进入基础业务态，但所有依赖绝对时间的功能保持未激活状态。 |
| `用户命令 (User Command)` | 用户通过独立按键发出关机指令 | **前置状态 (Pre-condition):** 系统处于正常工作状态或设置状态<br>**动作 (Action):** 执行配置保存、停止播报、关闭背光并退出业务任务，进入关机状态。 | **条件 (Condition):** 当前存在未完成的配置写入、闹钟修改或云端同步任务<br>**动作 (Action):** 延迟关机，待当前安全收尾完成后再执行关机；不默认进入 deep sleep 持续待机。 |

### Feature (功能): F02_WiFiTimeSync
**功能概述 (Feature Overview):** 设备联网后同步网络时间并建立系统时间基线，为显示、闹钟与代办提醒提供有效时基；该时基在断电后默认不保留。

| 触发类型 (Trigger Type) | 触发条件描述 (Trigger Description) | 预期响应 (Expected Response) | 例外处理 (Exception Handling) |
| :--- | :--- | :--- | :--- |
| `系统事件 (System Event)` | WiFi 连接成功后首次触发校时，或到达周期性校时点 | **前置状态 (Pre-condition):** WiFi 已连接，系统时间服务可更新<br>**动作 (Action):** 获取网络标准时间，建立或修正系统时间基线，并刷新屏幕中的时间与日期显示。 | **条件 (Condition):** 网络不可用、时间服务器无响应或返回时间非法<br>**动作 (Action):** 保持当前系统时间估算继续运行；若设备尚未完成首次校时，则保持“时间未同步”状态并等待下一个同步周期。 |
| `系统事件 (System Event)` | 周期性校时与云端代办同步在同一时刻触发 | **前置状态 (Pre-condition):** WiFi 业务调度器正在运行<br>**动作 (Action):** 按业务调度顺序发起时间同步。 | **条件 (Condition):** 云端代办同步正在占用 WiFi 通道<br>**动作 (Action):** **与[F15_TodoCloudSync]存在并发冲突，需要增加并发保护机制。** |

## 模块类别 (Module Category): 显示与环境感知

### Feature (功能): F03_MainDashboardDisplay
**功能概述 (Feature Overview):** TFT 彩屏持续显示时间、日期、温湿度、光照、闹钟状态和代办事项。

| 触发类型 (Trigger Type) | 触发条件描述 (Trigger Description) | 预期响应 (Expected Response) | 例外处理 (Exception Handling) |
| :--- | :--- | :--- | :--- |
| `系统事件 (System Event)` | 秒级时间刷新、环境数据更新、闹钟配置变更或代办缓存更新 | **前置状态 (Pre-condition):** 显示屏已初始化，系统处于亮屏工作状态<br>**动作 (Action):** 刷新主界面中的时间、日期、温湿度、光照、闹钟摘要和代办事项摘要。 | **条件 (Condition):** 屏幕驱动初始化异常、SPI 总线忙、刷新超时，或系统时间尚未完成首次同步<br>**动作 (Action):** 保持上一帧有效界面；若时间无效，则时间和日期区域显示占位状态并等待校时成功。 |
| `用户命令 (User Command)` | 用户通过按键切换显示内容或进入设置界面 | **前置状态 (Pre-condition):** 主界面处于可交互状态<br>**动作 (Action):** 切换显示焦点或跳转至设置界面，并保持状态信息可见。 | **条件 (Condition):** 当前屏幕刷新任务正在长时间占用显示总线<br>**动作 (Action):** **与[F17_SystemParameterSetting]存在并发冲突，需要增加并发保护机制。** |

### Feature (功能): F04_EnvironmentSampling
**功能概述 (Feature Overview):** 系统按照设定周期采集温度、湿度和光照数据，并为显示与语音逻辑提供统一环境缓存。

| 触发类型 (Trigger Type) | 触发条件描述 (Trigger Description) | 预期响应 (Expected Response) | 例外处理 (Exception Handling) |
| :--- | :--- | :--- | :--- |
| `系统事件 (System Event)` | 到达用户设定的环境参数采集周期 | **前置状态 (Pre-condition):** DHT11 与 BH1750 已初始化<br>**动作 (Action):** 读取温度、湿度和光照数据，更新环境缓存并通知显示与语音判断模块。 | **条件 (Condition):** 传感器读数超时、校验失败或返回异常值<br>**动作 (Action):** 保留最近一次有效数据，并标记当前采样结果无效。 |
| `系统事件 (System Event)` | 环境采样与显示刷新同时访问环境缓存 | **前置状态 (Pre-condition):** 数据缓存正在被多个任务消费<br>**动作 (Action):** 执行环境数据更新。 | **条件 (Condition):** 主界面刷新正在读取同一份环境缓存<br>**动作 (Action):** **与[F03_MainDashboardDisplay]存在并发冲突，需要增加并发保护机制。** |

### Feature (功能): F05_BacklightBrightnessControl
**功能概述 (Feature Overview):** 系统依据用户设置与屏幕状态控制背光亮度，在可视性与功耗之间切换。

| 触发类型 (Trigger Type) | 触发条件描述 (Trigger Description) | 预期响应 (Expected Response) | 例外处理 (Exception Handling) |
| :--- | :--- | :--- | :--- |
| `用户命令 (User Command)` | 用户在设置界面调整亮度参数 | **前置状态 (Pre-condition):** 系统处于设置菜单，背光 PWM 通道可用<br>**动作 (Action):** 更新背光亮度占空比，并保存亮度配置。 | **条件 (Condition):** 输入亮度越界或背光 PWM 输出异常<br>**动作 (Action):** 拒绝本次修改并保持原有亮度配置。 |
| `系统事件 (System Event)` | 系统进入亮屏、极简显示或息屏状态 | **前置状态 (Pre-condition):** 屏幕工作模式切换条件成立<br>**动作 (Action):** 按当前模式切换背光亮度或关闭背光。 | **条件 (Condition):** 背光控制任务与设置写入同时修改亮度参数<br>**动作 (Action):** **与[F17_SystemParameterSetting]存在并发冲突，需要增加并发保护机制。** |

## 模块类别 (Module Category): 人体存在检测与低功耗显示

### Feature (功能): F06_PresenceWakeAndSleep
**功能概述 (Feature Overview):** 设备根据人体存在检测结果自动亮屏、息屏并切换到低功耗显示策略。

| 触发类型 (Trigger Type) | 触发条件描述 (Trigger Description) | 预期响应 (Expected Response) | 例外处理 (Exception Handling) |
| :--- | :--- | :--- | :--- |
| `硬件事件 (Hardware Event)` | HLK-LD2410C 检测到有人靠近，且当前处于息屏或极简显示状态 | **前置状态 (Pre-condition):** 人体存在检测模块已工作，屏幕处于低功耗显示态<br>**动作 (Action):** 点亮屏幕，恢复主界面正常显示，并允许后续问候语触发。 | **条件 (Condition):** 雷达信号连续抖动或短时间内重复触发<br>**动作 (Action):** 启动存在检测防抖，不重复执行亮屏动作。 |
| `硬件事件 (Hardware Event)` | 人体离开持续达到 3 分钟 | **前置状态 (Pre-condition):** 屏幕处于正常亮屏显示状态<br>**动作 (Action):** 关闭背光或切换至极简显示模式，降低系统显示功耗；系统可进一步进入由 `ESP-IDF` 电源管理接管的轻睡眠候选状态。 | **条件 (Condition):** 离开时长未达到 3 分钟且期间重新检测到有人，或当前平台约束不允许进入 deep sleep 以维持快速唤醒体验<br>**动作 (Action):** 取消息屏流程并保持当前亮屏状态；低功耗策略维持在 light sleep 候选级别。 |

### Feature (功能): F07_PresenceSessionTracking
**功能概述 (Feature Overview):** 系统维护连续在位时长，用于支持休息提醒与会话级语音策略。

| 触发类型 (Trigger Type) | 触发条件描述 (Trigger Description) | 预期响应 (Expected Response) | 例外处理 (Exception Handling) |
| :--- | :--- | :--- | :--- |
| `系统事件 (System Event)` | 设备持续检测到用户在位 | **前置状态 (Pre-condition):** 人体检测结果有效，系统处于工作状态<br>**动作 (Action):** 累计在位时长，并将会话状态提供给休息提醒与问候逻辑使用。 | **条件 (Condition):** 雷达读数异常或检测结果短时间跳变<br>**动作 (Action):** 保持上一轮有效在位状态，并等待下一次稳定结果。 |
| `系统事件 (System Event)` | 用户短暂离开后重新出现，离开间隔不超过 10 分钟 | **前置状态 (Pre-condition):** 会话计时已经开始<br>**动作 (Action):** 继续累计当前会话时长，不中断休息提醒计时。 | **条件 (Condition):** 离开时长超过 10 分钟<br>**动作 (Action):** 终止当前会话计时，并在重新检测到稳定在位后重新开始计时。 |

## 模块类别 (Module Category): 语音播报服务

### Feature (功能): F08_PresenceGreetingVoice
**功能概述 (Feature Overview):** 在息屏状态下首次检测到用户时，系统自动播放一次欢迎语音。

| 触发类型 (Trigger Type) | 触发条件描述 (Trigger Description) | 预期响应 (Expected Response) | 例外处理 (Exception Handling) |
| :--- | :--- | :--- | :--- |
| `硬件事件 (Hardware Event)` | 息屏或极简显示状态下首次检测到用户出现 | **前置状态 (Pre-condition):** 当前会话尚未执行过问候播报，音频输出链路可用<br>**动作 (Action):** 播放一次问候语音，并标记本次用户会话已问候。 | **条件 (Condition):** 当前会话已经问候过或音频通道被更高优先级语音占用<br>**动作 (Action):** 跳过本次问候并等待下一次有效会话。 |

### Feature (功能): F09_EnvironmentVoicePrompt
**功能概述 (Feature Overview):** 当温湿度或光照越界时，系统按阈值规则播报环境建议并执行冷却控制。

| 触发类型 (Trigger Type) | 触发条件描述 (Trigger Description) | 预期响应 (Expected Response) | 例外处理 (Exception Handling) |
| :--- | :--- | :--- | :--- |
| `系统事件 (System Event)` | 温度、湿度或光照判定为过高、过低或超出舒适区间 | **前置状态 (Pre-condition):** 环境缓存有效，语音提示开关已启用<br>**动作 (Action):** 播报对应建议，例如开灯、关灯、开窗、保暖或开启空调，并启动 1 至 3 分钟冷却窗口。 | **条件 (Condition):** 同类环境提示仍处于冷却窗口，或对应语音提示被用户关闭<br>**动作 (Action):** 抑制本次播报，仅保留环境异常状态用于显示。 |

### Feature (功能): F11_RestReminder
**功能概述 (Feature Overview):** 用户连续在位达到 4 小时后，系统提醒其注意休息，并限制重复打扰频率。

| 触发类型 (Trigger Type) | 触发条件描述 (Trigger Description) | 预期响应 (Expected Response) | 例外处理 (Exception Handling) |
| :--- | :--- | :--- | :--- |
| `系统事件 (System Event)` | 用户连续在位时长超过 4 小时，且中途离开间隔均不超过 10 分钟 | **前置状态 (Pre-condition):** 在位会话计时有效，休息提醒功能已启用<br>**动作 (Action):** 播放休息提醒语音，并启动 3 至 5 分钟冷却窗口。 | **条件 (Condition):** 当前提醒仍在冷却期，或会话连续性已被长时间离开打断<br>**动作 (Action):** 延后提醒或重置累计计时，不重复打扰用户。 |

### Feature (功能): F12_VoicePriorityScheduling
**功能概述 (Feature Overview):** 多个语音请求同时出现时，系统按照既定优先级逐个排队播报。

| 触发类型 (Trigger Type) | 触发条件描述 (Trigger Description) | 预期响应 (Expected Response) | 例外处理 (Exception Handling) |
| :--- | :--- | :--- | :--- |
| `系统事件 (System Event)` | 问候、环境提示、休息提醒、闹钟提醒或代办提醒在同一时间窗口同时触发 | **前置状态 (Pre-condition):** 语音调度器已启动，音频输出链路可用<br>**动作 (Action):** 按“问候语、环境提示、休息提醒、闹钟/代办提醒”的优先级顺序逐个播报。 | **条件 (Condition):** 语音请求同时抢占同一 I2S 音频输出资源<br>**动作 (Action):** **与[F08_PresenceGreetingVoice、F09_EnvironmentVoicePrompt、F11_RestReminder、F14_AlarmTriggerAndReminder、F16_TodoVoiceReminder]存在并发冲突，需要增加并发保护机制。** |

## 模块类别 (Module Category): 闹钟与代办事项

### Feature (功能): F13_AlarmConfiguration
**功能概述 (Feature Overview):** 用户可通过按键新增、删除和修改闹钟，并配置是否重复执行；在时间基线无效时仅允许保存配置，不允许进入激活状态。

| 触发类型 (Trigger Type) | 触发条件描述 (Trigger Description) | 预期响应 (Expected Response) | 例外处理 (Exception Handling) |
| :--- | :--- | :--- | :--- |
| `用户命令 (User Command)` | 用户进入闹钟设置并执行新增、删除或修改操作 | **前置状态 (Pre-condition):** 系统处于设置菜单，闹钟配置区可写<br>**动作 (Action):** 更新闹钟的时、分、秒、重复属性和启用状态，并刷新主界面的闹钟摘要。 | **条件 (Condition):** 输入时间非法、闹钟数量达到上限、配置写入失败，或当前系统尚未具备有效时间基线<br>**动作 (Action):** 拒绝本次修改或将闹钟保留为未激活状态，直到首次校时成功。 |
| `用户命令 (User Command)` | 用户保存闹钟配置 | **前置状态 (Pre-condition):** 闹钟配置内容通过格式检查<br>**动作 (Action):** 将闹钟配置写入本地非易失存储。 | **条件 (Condition):** 系统参数设置任务正在写入同一配置存储区<br>**动作 (Action):** **与[F17_SystemParameterSetting]存在并发冲突，需要增加并发保护机制。** |

### Feature (功能): F14_AlarmTriggerAndReminder
**功能概述 (Feature Overview):** 当系统时间基线到达有效闹钟时间时，系统触发语音与界面提醒，直到用户处理；该功能不承担断电恢复后的自动补触发责任。

| 触发类型 (Trigger Type) | 触发条件描述 (Trigger Description) | 预期响应 (Expected Response) | 例外处理 (Exception Handling) |
| :--- | :--- | :--- | :--- |
| `系统事件 (System Event)` | 系统时间匹配某个已启用闹钟的时、分、秒 | **前置状态 (Pre-condition):** 闹钟配置有效，系统时间已经完成首次校时<br>**动作 (Action):** 切换至闹钟提醒状态，播放闹钟语音并在屏幕上突出显示闹钟信息。 | **条件 (Condition):** 音频输出链路忙碌、闹钟配置数据损坏，或当前系统时间无效<br>**动作 (Action):** 保留闹钟待提醒标志；若时间无效，则禁止触发绝对时间闹钟并提示用户先完成校时。 |

### Feature (功能): F15_TodoCloudSync
**功能概述 (Feature Overview):** 设备通过 WiFi 周期性同步云端代办事项，并维护本地待办缓存。

| 触发类型 (Trigger Type) | 触发条件描述 (Trigger Description) | 预期响应 (Expected Response) | 例外处理 (Exception Handling) |
| :--- | :--- | :--- | :--- |
| `系统事件 (System Event)` | WiFi 可用且到达代办同步周期 | **前置状态 (Pre-condition):** 云端接口可访问，本地代办缓存可写<br>**动作 (Action):** 拉取最新代办事项，更新本地缓存，并通知主界面刷新代办摘要。 | **条件 (Condition):** 网络不可用、云端接口异常或返回数据格式错误<br>**动作 (Action):** 保留本地代办缓存并等待下一次同步周期。 |
| `系统事件 (System Event)` | 代办同步与网络校时同时触发 | **前置状态 (Pre-condition):** WiFi 任务调度器正在运行<br>**动作 (Action):** 执行代办同步任务。 | **条件 (Condition):** 网络校时任务已占用 WiFi 通道<br>**动作 (Action):** **与[F02_WiFiTimeSync]存在并发冲突，需要增加并发保护机制。** |

### Feature (功能): F16_TodoVoiceReminder
**功能概述 (Feature Overview):** 当代办事项到达预设提醒时间时，系统执行纯语音提醒并同步屏幕提示；若设备掉电重启且未重新校时，则不保证提醒连续性。

| 触发类型 (Trigger Type) | 触发条件描述 (Trigger Description) | 预期响应 (Expected Response) | 例外处理 (Exception Handling) |
| :--- | :--- | :--- | :--- |
| `系统事件 (System Event)` | 某条本地代办事项到达提醒时间 | **前置状态 (Pre-condition):** 代办事项已同步到本地、提醒开关有效，且系统时间已经完成首次校时<br>**动作 (Action):** 播报代办事项内容，并在屏幕上显示当前待办提醒。 | **条件 (Condition):** 当前代办已经在本提醒周期播报过、语音通道正在处理更高优先级任务，或当前系统时间无效<br>**动作 (Action):** 避免重复提醒，并交由语音优先级调度器排队；若时间无效，则暂停基于绝对时间的代办提醒。 |

## 模块类别 (Module Category): 系统设置

### Feature (功能): F17_SystemParameterSetting
**功能概述 (Feature Overview):** 用户通过 4 个独立按键调整静音、语音输出、亮度、音量和环境采样周期等系统参数。

| 触发类型 (Trigger Type) | 触发条件描述 (Trigger Description) | 预期响应 (Expected Response) | 例外处理 (Exception Handling) |
| :--- | :--- | :--- | :--- |
| `用户命令 (User Command)` | 用户在设置界面调整静音开关、音量、亮度或环境采样周期 | **前置状态 (Pre-condition):** 系统处于设置菜单，参数编辑功能可用<br>**动作 (Action):** 更新对应系统参数，立即生效，并保存到本地配置区。 | **条件 (Condition):** 参数越界、本地配置写入失败或按键输入无效<br>**动作 (Action):** 回退到最近一次有效配置，并提示用户本次设置未生效。 |

### Feature (功能): F18_VoicePreferenceSetting
**功能概述 (Feature Overview):** 用户可配置部分语音提示是否关闭，以及语音重复播报次数。

| 触发类型 (Trigger Type) | 触发条件描述 (Trigger Description) | 预期响应 (Expected Response) | 例外处理 (Exception Handling) |
| :--- | :--- | :--- | :--- |
| `用户命令 (User Command)` | 用户在语音设置菜单中关闭部分提示或修改语音重复次数 | **前置状态 (Pre-condition):** 系统处于设置菜单，语音配置区可写<br>**动作 (Action):** 更新语音提示开关与重复次数策略，并保存到本地配置。 | **条件 (Condition):** 配置值非法、写入失败或系统参数设置任务正在访问同一配置区<br>**动作 (Action):** **与[F17_SystemParameterSetting]存在并发冲突，需要增加并发保护机制。** |
