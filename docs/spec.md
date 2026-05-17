# ESP32 智能语音办公闹钟 技术选型与规格定义书 (Technical Spec Document)

## 一、 硬件选型 (Hardware Selection)
| 组件类别 | 硬件型号/模块名称 | 接口规格/开发板外设描述 |
| :--- | :--- | :--- |
| **主控核心 (SoC/MCU)** | ESP32-S3-DevKitC-1 | 软件框架冻结为 `ESP-IDF`；集成 2.4 GHz Wi-Fi 与 Bluetooth LE；开发板排针引出除 Flash SPI 总线外的大部分 GPIO；SoC 侧提供 4x SPI、2x I2C、3x UART、2x I2S、LEDC PWM、USB OTG；当前 bring-up 默认按 `16 MB Flash` 实物容量构建，以避免镜像头与硬件容量不一致。 |
| **显示模块** | HS20HS072RX TFT LCD + FPC12P 转接板 | 2.4 英寸 TFT，分辨率 240x320，控制器/驱动 IC 为 ST7789T3，数据接口 `SPI 4W`；IOVCC 1.65 V 至 3.3 V，VCI 2.4 V 至 3.3 V；背光为 4 颗白光 LED 并联，典型工作点约 `3.0 V / 80 mA`，需由外部限流与低边 NMOS 开关驱动，MCU GPIO 仅输出 PWM 控制信号。 |
| **温湿度传感器模块** | DHT11 | 单总线数字接口；在 `ESP-IDF` 中通过 GPIO 时序读取实现，数据线需上拉。 |
| **光照传感器模块** | GY-302 / BH1750FVI | I2C 通讯，SCL 频率最高 400 kHz；7-bit 从地址由 ADDR 管脚决定，`ADDR=L -> 0x23`，`ADDR=H -> 0x5C`；VCC 2.4 V 至 3.6 V。 |
| **人体存在检测模块** | HLK-LD2410C | 24 GHz 毫米波人体存在传感模组；支持 `GPIO + UART` 双输出；供电输入 5 V，供电能力要求大于 200 mA；目标状态 OUT 脚输出 3.3 V 电平；TTL 串口默认 256000 bps，1 停止位，无奇偶校验；最远探测距离 6 m，探测角度 ±60°，距离分辨率 0.75 m。 |
| **音频输出模块** | MAX98357A I2S 单声道功放模块 | 数字音频输入为 I2S；支持 8 kHz 至 96 kHz 采样率；无需 MCLK；逻辑输入兼容 3.3 V 至 5 V；功放供电 2.7 V 至 5.5 V；外接 4 Ω 或 8 Ω 动圈扬声器。 |
| **人机输入模块** | 独立按键 ×4 | 每个按键占用 1 路 GPIO；建议以 GPIO 中断输入方式接入，并在软件层增加去抖；用于系统设置、闹钟设置、语音设置与开关机输入。 |

## 二、 软件栈基础 (Software Stack Foundation)
| 软件层级 | 官方名称与技术选型 | 说明 |
| :--- | :--- | :--- |
| **最小系统与内核 (Kernel)** | ESP-IDF + FreeRTOS | 提供线程调度、软件定时器、事件循环、系统时基与电源状态切换基础能力；本项目的绝对时间功能建立在首次联网校时后的系统时间基线上，不依赖断电保持 RTC。 |
| **驱动接口支持 (Drivers)** | ESP-IDF GPIO / I2C Master / SPI Master / `esp_lcd` ST7789 / UART / LEDC / I2S / NVS Flash / Wi-Fi Driver | 提供 DHT11、BH1750、ST7789 面板、LD2410C、按键、背光 PWM 控制、音频输出、内部 Flash 与时钟/网络底层访问能力。 |
| **上层子系统 (Subsystems)** | LVGL / esp_netif / esp_event / lwIP Sockets / SNTP / HTTP Client / NVS / FreeRTOS | 显示层使用 `esp_lcd` 与 LVGL；按键、雷达、音频和环境传感器通过 `ESP-IDF` 驱动层接入；联网、校时与云端代办同步依赖 Wi-Fi、Sockets、SNTP 与 HTTP Client；本地持久化采用 NVS。 |
| **开发工具 (Dev Tools)** | ESP Logging / idf.py Monitor / Console | 提供串口日志、运行态诊断与参数观察能力；适合校时状态、传感器原始值、雷达串口帧和网络同步结果调试。 |

## 三、 硬软映射关系 (Hardware-to-Software Mapping)
| 硬件名称 | 底层驱动 | 上层接口/子系统 |
| :--- | :--- | :--- |
| ESP32-S3 板载 Wi-Fi 无线单元 | ESP-IDF Wi-Fi Driver | esp_netif / esp_event / lwIP Sockets |
| ESP32-S3 板载 SPI Flash | ESP-IDF Flash / NVS Flash | NVS |
| ESP32-S3 软件时基 | FreeRTOS Tick / esp_timer / SNTP 同步后系统时间 | FreeRTOS Timing / time.h |
| HS20HS072RX (ST7789T3) 显示屏 | `esp_lcd` ST7789 + SPI Master | LVGL |
| DHT11 | GPIO Driver + 本地时序读取 | environment_service |
| BH1750FVI | I2C Master Driver | environment_service |
| HLK-LD2410C | UART Driver / GPIO Driver | presence_service |
| MAX98357A 功放模块 | I2S Driver | audio_service |
| 背光控制支路 (GPIO9 + NMOS) | LEDC PWM Driver | backlight_service |
| 独立按键 ×4 | GPIO Driver | input_service |

## 四、 功软映射关系 (Feature-to-Software Mapping)
*(注：本表仅描述业务功能与接口/子系统的调用关系，不涉及任务编排与资源同步机制等架构实现细节)*
| 功能编号 | 关联上层接口/子系统 |
| :--- | :--- |
| F01 | FreeRTOS / NVS / esp_pm |
| F02 | esp_wifi / lwIP Sockets / SNTP |
| F03 | esp_lcd / LVGL / environment_service |
| F04 | GPIO / I2C Master |
| F05 | LEDC PWM / NVS |
| F06 | UART / GPIO / 电源策略 |
| F07 | UART / GPIO / FreeRTOS Timing |
| F08 | GPIO / I2S |
| F09 | GPIO / I2C / I2S / NVS |
| F10 | FreeRTOS Timing / I2S |
| F11 | FreeRTOS Timing / GPIO / I2S |
| F12 | I2S / NVS |
| F13 | GPIO / LVGL / NVS |
| F14 | FreeRTOS Timing / I2S / LVGL |
| F15 | esp_wifi / lwIP Sockets / HTTP Client / NVS |
| F16 | NVS / I2S / LVGL |
| F17 | GPIO / LEDC / NVS |
| F18 | GPIO / NVS |
