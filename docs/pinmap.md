# ESP32 智能语音办公闹钟 引脚映射定义 (Pin Mapping Definition)

| 开发板引脚 (Board Pin) | 外设引脚 (Peripheral Pin) |
| :--- | :--- |
| J1.1 `3V3` | HS20HS072RX.VCC |
| J1.1 `3V3` | HS20HS072RX.IOVCC |
| J1.22 `G` | HS20HS072RX.GND |
| J1.18 `GPIO12` | HS20HS072RX.SCL |
| J1.17 `GPIO11` | HS20HS072RX.SDA |
| J1.16 `GPIO10` | HS20HS072RX.CS |
| J1.4 `GPIO4` | HS20HS072RX.DC |
| J1.12 `GPIO8` | HS20HS072RX.RST |
| J1.15 `GPIO9` | BL_CTRL_NMOS.GATE |
| J1.21 `5V` | BL_LIMIT_RES.IN |
| BL_LIMIT_RES.OUT | HS20HS072RX.BL+ |
| HS20HS072RX.BL- | BL_CTRL_NMOS.DRAIN |
| J1.22 `G` | BL_CTRL_NMOS.SOURCE |
| J3.4 `GPIO1` | BH1750FVI.SDA |
| J3.5 `GPIO2` | BH1750FVI.SCL |
| J1.1 `3V3` | BH1750FVI.VCC |
| J3.1 `G` | BH1750FVI.GND |
| J3.1 `G` | BH1750FVI.ADDR |
| J1.13 `GPIO3` | DHT11.DATA |
| J1.1 `3V3` | DHT11.VCC |
| J1.22 `G` | DHT11.GND |
| J1.10 `GPIO17` | HLK-LD2410C.UART_Rx |
| J1.11 `GPIO18` | HLK-LD2410C.UART_Tx |
| J1.9 `GPIO16` | HLK-LD2410C.OUT |
| J1.21 `5V` | HLK-LD2410C.VCC |
| J3.1 `G` | HLK-LD2410C.GND |
| J1.19 `GPIO13` | MAX98357A.BCLK |
| J1.20 `GPIO14` | MAX98357A.LRC |
| J3.18 `GPIO21` | MAX98357A.DIN |
| J1.21 `5V` | MAX98357A.VIN |
| J3.1 `G` | MAX98357A.GND |
| J3.9 `GPIO40` | KEY1.SIG |
| J3.8 `GPIO41` | KEY2.SIG |
| J3.7 `GPIO42` | KEY3.SIG |
| J3.15 `GPIO45` | KEY4.SIG |
| J3.1 `G` | KEY1.GND |
| J3.1 `G` | KEY2.GND |
| J3.1 `G` | KEY3.GND |
| J3.1 `G` | KEY4.GND |

## 连接说明 (Connection Notes)

- 主控开发板为 `ESP32-S3-DevKitC-1 v1.1`；避开了板载占用或不建议复用的 `GPIO35/36/37`、板载 RGB LED 所在 `GPIO38` 以及 USB `GPIO19/20`。
- TFT 显示屏使用一组独立 SPI 连接：`GPIO12=SCLK`、`GPIO11=MOSI`、`GPIO10=CS`，并单独分配 `GPIO4=DC`、`GPIO8=RST`；`GPIO9` 不直接驱动背光电流，而是作为 `LEDC PWM` 输出连接到低边 `NMOS Gate`；该组连接用于 `zephyr,mipi-dbi-spi + sitronix,st7789v`。
- 背光电源拓扑为：`5V -> 限流电阻 -> HS20HS072RX.BL+`，`HS20HS072RX.BL- -> NMOS Drain`，`NMOS Source -> GND`，`GPIO9 -> NMOS Gate`；屏幕背光典型工作点约 `3.0V / 80mA`，因此 MCU GPIO 不直接承担背光供电。
- `BL_CTRL_NMOS.GATE` 建议串联约 `100Ω` 栅极电阻，并以约 `100kΩ` 下拉到地；`BL_LIMIT_RES` 需按背光目标电流单独计算，或替换为恒流驱动。
- BH1750FVI 独占一条 I2C 总线连接：`GPIO1=SDA`、`GPIO2=SCL`；`ADDR` 接地，因此器件地址固定为 `0x23`，与系统内其他外设无地址冲突。
- DHT11 按用户提供的数据手册约束，以 `3V3` 供电，`DATA` 连接 `GPIO3`；数据线需要上拉到 `3V3`。
- LD2410C 使用 `UART1` 默认脚位方向：`GPIO17 -> 模块 UART_Rx`、`GPIO18 <- 模块 UART_Tx`；目标状态 `OUT` 连接 `GPIO16`；模块供电使用 `5V`，串口保持模块默认 `256000 bps / 1 stop bit / no parity`。
- MAX98357A 使用三线 I2S：`GPIO13=BCLK`、`GPIO14=LRC/WS`、`GPIO21=DIN`；模块供电使用 `5V` 以获得更高扬声器输出能力；`GAIN` 与 `SD/MODE` 使用模块默认绑线，不额外占用 MCU GPIO。
- 四个独立按键均采用“GPIO 对地”的接法，分别占用 `GPIO40`、`GPIO41`、`GPIO42`、`GPIO45`；软件侧应配置为输入上拉并按低电平按下处理。
