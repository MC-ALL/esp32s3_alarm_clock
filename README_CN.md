# Smart Clock（ESP-IDF）

本仓库包含一个基于 `ESP-IDF` 构建的 `ESP32-S3` 智能闹钟项目固件。

当前代码库已经不再只是基础 bring-up 脚手架。核心显示、环境感知、Todo/配置同步、音频播放、本地设置、状态上报以及页面交互链路都已经在固件中跑通。

## 1. 当前状态

### 1.1 当前可见效果

当前 UI 由 `LVGL` 驱动，并围绕以下真实主页面组织：

- `HOME`
- `ALARM`
- `TODO`
- `ENV`
- `WIFI`
- `LOW_CLOCK`

当前固件已经支持：

- 时间与日期显示
- 本地闹钟浏览与编辑
- Todo 列表显示与 Todo 项操作
- 环境数据展示
- 网络状态展示
- 用户离开时的低干扰时钟页面

### 1.2 当前功能实现情况

已经实现：

- `ESP-IDF` 项目结构与模块生命周期编排
- LCD / 背光 / 按键输入
- 本地设置模型与 NVS 持久化
- `BH1750` 光照采样
- `DHT11` 温湿度读取
- `LD2410C` 在位检测链路
- 基于 `I2S` 的 WAV 音频播放
- Wi-Fi STA 连接
- `SNTP` 时间同步
- Todo HTTP 同步、本地 Todo 缓存以及 Todo 项操作
- 设备配置拉取、状态上报与事件上报链路

部分实现：

- `DHT11` 仍然存在稳定性限制
- Todo/配置 JSON 解析目前已集中到 `sync_protocol`，并基于 `cJSON`
- 网络目标地址仍依赖配置中的 host / IP

尚未完全成熟：

- 更强的网络环境适配
- 更健壮的 Todo 协议解析
- 更完整的提醒策略细化
- 产品级运行时加固

## 2. 目录说明

### `src/`

ESP-IDF 应用组件、启动编排、模块调度和功能模块实现。

- `src/main.c`
- `src/app_boot.c`
- `src/app_module.c`
- `src/modules/interaction/`
  - 显示、背光、输入、UI 模型
- `src/modules/sensing/`
  - 环境采样、在位检测
- `src/modules/connectivity/`
  - Wi-Fi、`SNTP`、配置同步、状态上报、事件上报
- `src/modules/audio/`
  - 基于 `I2S` 的 WAV 播放
- `src/modules/config/`
  - 设置与持久化
- `src/modules/core/`
  - 生命周期 / 故障 / 时间基线骨架
- `src/modules/reminder/`
  - 提醒触发逻辑

### `include/`

公共头文件与共享配置。

关键文件：

- `include/core/hw_config.h`
- `include/core/app_config.h`
- `include/connectivity/net_service.h`
- `include/config/settings_model.h`

### `assets/`

固件和文档使用的源资源。

- `assets/audio/` 下的音频资源

### `docs/`

设计、规格、接口交接、硬件说明和参考资料。

## 3. 构建与运行

### 3.1 前置条件

- `ESP-IDF 6.0.1`
- 目标板：`ESP32-S3`
- 本地串口访问权限

### 3.2 构建

```bash
. /path/to/esp-idf/export.sh
idf.py set-target esp32s3
idf.py build
```

### 3.3 烧录与监视

```bash
idf.py -p /dev/ttyUSB0 flash monitor
```

请将串口替换为你本地实际使用的设备。

### 3.4 本地 Web 预览

如果你只是想在本地预览 Web 控制中心界面，可以将 Web 应用绑定在回环地址上启动，而不暴露到局域网中。

这种本地预览方式兼容 Windows，可以直接在 PowerShell、Windows Terminal 或其它本地命令行环境中启动。

在 `web/` 目录下执行：

```bash
pip install -r requirements.txt
python -m uvicorn app:app --host 127.0.0.1 --port 8078 --reload
```

然后打开：

```text
http://127.0.0.1:8078/status
```

其它本地页面包括：

- `http://127.0.0.1:8078/todos`
- `http://127.0.0.1:8078/alarms`
- `http://127.0.0.1:8078/voice`
- `http://127.0.0.1:8078/events`
- `http://127.0.0.1:8078/model`

这种仅回环地址模式主要用于查看页面布局和交互效果，不适合做真实的设备侧同步测试，因为设备无法访问你开发机上的 `127.0.0.1`。

当前 `/model` 页面行为：

- 模型对话是 Web 侧只读助手
- 隐藏对话概括保存在 Web 本地
- 健康分析报告只在用户主动点击生成时调用 AI
- PDF 导出复用最近一份已保存报告，不会再次请求 AI
- 真正执行 AI 调用仍依赖 Web 运行环境中可用的远端 AI 配置与 API key

## 4. 当前运行时说明

### 时间同步

当前时间同步使用：

- `SNTP`

行为：

- Wi-Fi 获取 IP 后同步一次
- 后续定期重新同步
- 固件中已经配置本地时区

### Todo 同步

当前 Todo/配置同步的工作方式为：

- 从配置的 Web 端点执行 HTTP 拉取
- 解析成功后替换本地缓存
- 使用 NVS 持久化缓存
- 支持手动同步触发
- 设备侧通过 HTTP 执行 Todo 完成 / 删除操作
- 设备侧对闹钟 / 语音设置的修改会立即请求更新 Web 真源

当前同步拓扑包括：

- 配置拉取：`Web -> device`
- 状态上报：`device -> Web`
- 事件上报：`device -> Web`

当前默认同步周期：

- 配置拉取：`30s`
- 状态上报：`10s`
- 事件上报：事件发生即上报

Todo 语义：

- 设备端只保存和显示未完成 Todo
- 已完成 Todo 在 Web 端归档，不会再拉回设备端
- 被删除的 Todo 会在 Web 端被永久移除

### 音频

当前音频播放使用：

- 嵌入式 `WAV`
- `PCM`
- `I2S`
- `MAX98357A`

当前事件类型包括：

- 欢迎提示
- Wi-Fi 连接提示
- Todo 同步提示
- 环境提醒
- 休息提醒
- 闹钟提醒

### 在位检测与低干扰时钟页

当前在位检测使用 `LD2410C`，包括：

- UART 数据
- `OUT` 引脚回退

`LOW_CLOCK` 页面是当前的低干扰显示模式。它会根据在位检测阈值自动进入与退出。

## 5. 本地配置

主要运行时配置位于：

- `include/core/app_config.h`

当前包括：

- Wi-Fi SSID / 密码
- SNTP 服务器
- Web host / port
- Todo API 路径
- device config/status/events API 路径

引脚映射位于：

- `include/core/hw_config.h`

## 6. 交接说明

当你需要反馈运行问题时，最有价值的信息包括：

1. `idf.py build` 的第一段错误输出
2. 串口启动 / 运行日志
3. 屏幕行为描述
4. 与 `hw_config.h` 相比的接线差异

这个仓库更适合理解为一个主链路已经跑通的嵌入式原型，而不是仅仅停留在硬件 bring-up 阶段。
