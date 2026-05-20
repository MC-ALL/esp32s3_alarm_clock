# ESP32 UI Integration API

## 1. 目标

本文档用于给负责 `ESP32` 固件、按键驱动、雷达/传感器采集、本地服务进程、以及后续 `LVGL` 页面实现的同学对接。

这份文档回答四件事：
- UI 运行在 `ESP32` 上时，逻辑上需要做哪些调整
- 按键、雷达、环境传感器应如何向 UI 提供接口
- UI 应如何向平台层发出动作请求
- `Todo List` 与时间同步时，`ESP32` 和本地服务进程之间怎样对接

## 2. 逻辑调整结论

当前原型可以继续作为页面与状态机参考，但真上 `ESP32` 时，需要按下面的边界实现：

1. UI 不直接控制硬件，也不直接发网络请求。
   UI 只做两件事：接收状态快照，发出用户动作请求。

2. 所有 `LVGL/UI` 更新必须集中在单一 UI 任务里完成。
   雷达任务、传感器任务、按键扫描任务、本地服务同步任务都不能直接改界面控件，只能推状态或发消息。

3. 进入极简模式的条件要固定为：
   `radar_detected == false && absence_duration_ms >= threshold_ms && inactivity_duration_ms >= threshold_ms`

4. 用户操作只刷新“无操作计时”，不伪装成“雷达有人”。
   也就是说：
   - 雷达状态由雷达模块独立负责
   - 按键事件只负责页面操作，并刷新 `inactivity_duration_ms`

5. 时间同步和 `Todo List` 同步必须是异步命令。
   UI 发出“同步时间”或“同步 Todo”请求后，只能等待平台层或本地服务回写新状态，不能像浏览器原型那样直接改成功状态。

6. 环境参数必须带有效位和更新时间。
   因为真实传感器会出现采样失败、超时、数据过旧、初始化未完成等情况。

7. `power off` 只能是请求，不是直接执行。
   UI 发送关机请求，由平台层真正执行；执行完成后再把 `power_state=off` 回写给 UI。

## 3. 推荐模块划分

建议把系统拆成 5 个模块：

1. `ui_task`
   - 唯一允许操作 `LVGL/UI` 的任务
   - 消费按钮事件、状态更新事件、服务同步结果

2. `input_task`
   - 扫描 `K1-K4`
   - 生成按键事件

3. `sensor_task`
   - 采集 `DHT11`、`BH1750`
   - 更新环境状态

4. `radar_task`
   - 读取雷达识别结果
   - 维护“当前是否有人”与“连续无人时长”

5. `service_sync_task`
   - 通过 Wi-Fi 与本地服务进程通信
   - 同步时间与 `Todo List`
   - 回写同步状态与错误信息

## 4. ESP32 内部接口

下面是推荐给固件同学的对接方式。语言上以 `C/C++` 风格示意，但不强制。

### 4.1 按键输入接口

```c
typedef enum {
    UI_KEY_K1 = 0,
    UI_KEY_K2 = 1,
    UI_KEY_K3 = 2,
    UI_KEY_K4 = 3
} ui_key_id_t;

typedef enum {
    UI_KEY_EVENT_SHORT_PRESS = 0,
    UI_KEY_EVENT_LONG_PRESS = 1,
    UI_KEY_EVENT_REPEAT = 2
} ui_key_event_type_t;

typedef struct {
    ui_key_id_t key;
    ui_key_event_type_t type;
    uint32_t ts_ms;
} ui_button_event_t;
```

UI 输入函数建议为：

```c
void ui_handle_button_event(const ui_button_event_t *event);
```

当前页面至少要求支持：
- `SHORT_PRESS`

建议预留：
- `LONG_PRESS`
- `REPEAT`

对接规则：
- 任何有效按键事件都要刷新 `inactivity_duration_ms`
- 但按键事件不能改雷达状态
- `REPEAT` 仅用于设置页/闹钟编辑态的连续增减，若当前不做可先不发

### 4.2 雷达输入接口

```c
typedef struct {
    bool detected;
    bool radar_healthy;
    uint32_t absence_duration_ms;
    uint32_t inactivity_duration_ms;
    uint32_t threshold_ms;
    uint32_t updated_at_ms;
} ui_presence_state_t;
```

UI 输入函数建议为：

```c
void ui_update_presence(const ui_presence_state_t *state);
```

对接规则：
- `detected=true` 表示雷达当前识别到人
- `absence_duration_ms` 由雷达侧或平台层计算
- `inactivity_duration_ms` 由平台层根据用户输入事件计算
- `threshold_ms` 建议作为配置项，从平台层统一下发
- 如果 `radar_healthy=false`，UI 不应自动切极简

### 4.3 环境传感器输入接口

```c
typedef struct {
    bool valid_temperature;
    bool valid_humidity;
    bool valid_lux;
    float temperature_c;
    float humidity_percent;
    uint32_t lux;
    uint32_t updated_at_ms;
    uint32_t sample_interval_ms;
} ui_environment_state_t;
```

UI 输入函数建议为：

```c
void ui_update_environment(const ui_environment_state_t *state);
```

对接规则：
- 任一字段无效时，UI 显示 `--`
- `updated_at_ms` 过旧时，可视为 stale
- `sample_interval_ms` 应与设置页中的采样周期一致

### 4.4 时钟输入接口

```c
typedef struct {
    uint64_t unix_ms;
    bool synced;
    bool sync_in_progress;
    uint32_t last_sync_ms;
} ui_clock_state_t;
```

UI 输入函数建议为：

```c
void ui_update_clock(const ui_clock_state_t *state);
```

对接规则：
- `unix_ms` 是当前设备时间
- `synced=false` 时，UI 显示 `UNSYNC`
- 时间展示由 UI 每秒刷新；平台层不需要每秒都推完整页面状态，但至少要保证时间基准正确

### 4.5 Todo 输入接口

```c
#define UI_TODO_MAX_ITEMS 8
#define UI_TODO_TEXT_MAX_LEN 96

typedef struct {
    char id[24];
    char text[UI_TODO_TEXT_MAX_LEN];
    bool done;
    uint8_t priority;
    uint64_t due_unix_ms;
} ui_todo_item_t;

typedef struct {
    ui_todo_item_t items[UI_TODO_MAX_ITEMS];
    uint8_t count;
    bool sync_in_progress;
    bool sync_ok;
    uint32_t last_sync_ms;
    char last_error[64];
} ui_todo_state_t;
```

UI 输入函数建议为：

```c
void ui_update_todo_state(const ui_todo_state_t *state);
```

对接规则：
- 首页只显示前 `3-5` 条
- 超出部分用 `+N` 表示
- `sync_in_progress`、`sync_ok`、`last_error` 供网络页与提示区展示

### 4.6 闹钟输入接口

```c
#define UI_ALARM_MAX_ITEMS 8

typedef struct {
    char id[24];
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint8_t weekdays_mask;
    bool repeat;
    bool enabled;
} ui_alarm_item_t;

typedef struct {
    ui_alarm_item_t items[UI_ALARM_MAX_ITEMS];
    uint8_t count;
} ui_alarm_state_t;
```

UI 输入函数建议为：

```c
void ui_update_alarm_state(const ui_alarm_state_t *state);
```

说明：
- `weekdays_mask` 建议按 bitmask 传递，便于固件侧存储
- UI 负责转成页面里的星期文本
- 单次闹钟触发完成后，不再保留在闹钟列表中

## 5. UI 向平台层输出的动作接口

不建议让 UI 直接调用 Wi-Fi、雷达、RTC、文件、网络等实现逻辑。  
推荐由 UI 发 `action`，平台层接收后执行。

网络页交互补充：
- 页面先处于“模块选择态”
- `WiFi状态`、`动作`、`固定热点`、`同步状态` 作为 4 个大模块切换
- 进入 `固定热点` 模块后，选中热点并按 `K3`，默认映射为 `UI_ACTION_WIFI_CONNECT`
- `动作` 模块只保留：立即重连、校时、同步 Todo

### 5.1 动作枚举

```c
typedef enum {
    UI_ACTION_SET_SOUND_MUTED = 0,
    UI_ACTION_SET_AUDIO_ENABLED,
    UI_ACTION_SET_VOLUME,
    UI_ACTION_SET_ENV_SAMPLE_INTERVAL,
    UI_ACTION_SET_VOICE_FEATURE,
    UI_ACTION_SET_VOICE_REPEAT_COUNT,
    UI_ACTION_CREATE_ALARM,
    UI_ACTION_UPDATE_ALARM,
    UI_ACTION_DELETE_ALARM,
    UI_ACTION_TOGGLE_ALARM,
    UI_ACTION_WIFI_SCAN,
    UI_ACTION_WIFI_CONNECT,
    UI_ACTION_WIFI_DISCONNECT,
    UI_ACTION_SYNC_TIME,
    UI_ACTION_SYNC_TODO,
    UI_ACTION_REQUEST_SHUTDOWN
} ui_action_type_t;
```

### 5.2 动作载荷

```c
typedef struct {
    ui_action_type_t type;
    union {
        bool bool_value;
        int int_value;
        struct {
            char alarm_id[24];
        } delete_alarm;
        struct {
            char ssid[32];
            char password[64];
        } wifi_connect;
    } data;
} ui_action_t;
```

### 5.3 平台层回调

```c
typedef void (*ui_action_callback_t)(const ui_action_t *action, void *user_data);
```

初始化建议：

```c
void ui_init(ui_action_callback_t cb, void *user_data);
```

规则：
- UI 发动作后，不直接修改最终状态
- 平台层执行成功或失败后，再通过 `ui_update_*` 接口把最新状态回写给 UI

## 6. ESP32 与本地服务进程的接口

这里推荐使用 `HTTP + JSON`。  
如果团队最终使用 `UART`、`MQTT` 或别的传输方式，建议保留相同 payload 结构，避免页面层再改。

### 6.1 推荐职责边界

本地服务进程负责：
- 提供标准时间
- 提供 `Todo List`
- 处理同步状态与错误信息

ESP32 负责：
- 采集环境参数
- 管理按键与雷达
- 维护页面状态
- 调用本地服务接口并把结果映射到 UI

### 6.2 快照接口

建议优先提供一个聚合接口，减少 ESP32 往返次数：

`GET /api/v1/ui/snapshot`

示例响应：

```json
{
  "ok": true,
  "server_time": {
    "unix_ms": 1778848800000,
    "timezone": "Asia/Shanghai",
    "synced": true
  },
  "todo": {
    "items": [
      {
        "id": "todo-1",
        "text": "10:00 前确认会议纪要并发给项目组",
        "done": false,
        "priority": 2,
        "due_unix_ms": 1778852400000
      }
    ],
    "sync_ok": true,
    "last_sync_ms": 1778848780000,
    "last_error": ""
  }
}
```

### 6.3 主动校时接口

`POST /api/v1/time/sync`

请求体可为空：

```json
{}
```

响应：

```json
{
  "ok": true,
  "server_time": {
    "unix_ms": 1778848800000,
    "timezone": "Asia/Shanghai",
    "synced": true
  },
  "message": "time synced"
}
```

### 6.4 主动同步 Todo 接口

`POST /api/v1/todos/sync`

响应：

```json
{
  "ok": true,
  "todo": {
    "items": [
      {
        "id": "todo-1",
        "text": "10:00 前确认会议纪要并发给项目组",
        "done": false,
        "priority": 2,
        "due_unix_ms": 1778852400000
      }
    ],
    "sync_ok": true,
    "last_sync_ms": 1778848780000,
    "last_error": ""
  },
  "message": "todo synced"
}
```

### 6.5 错误响应

统一建议：

```json
{
  "ok": false,
  "code": "UPSTREAM_TIMEOUT",
  "message": "todo sync timeout"
}
```

规则：
- `message` 必须给人可读文本
- `code` 用于固件侧做日志与重试分类

## 7. 关键状态机规则

### 7.1 极简模式进入条件

```text
enter_minimal =
    (radar_detected == false)
 && (absence_duration_ms >= threshold_ms)
 && (inactivity_duration_ms >= threshold_ms)
 && (radar_healthy == true)
 && (power_state == on)
```

### 7.2 极简模式退出条件

满足任一条件即可退出：
- 雷达恢复检测到人
- 用户再次按键操作

### 7.3 无操作计时刷新条件

以下事件都应刷新 `inactivity_duration_ms`：
- `K1-K4` 有效按键事件
- 未来如增加旋钮、编码器、触摸等输入，也应计入

以下事件不应刷新：
- 雷达状态变化
- 定时同步成功
- 环境采样更新

## 8. 推荐对接顺序

1. 先完成 `ui_handle_button_event()` 对接
2. 再完成 `ui_update_presence()` 与极简模式切换
3. 再接 `ui_update_environment()` 与首页环境条
4. 再接 `ui_update_clock()` 与 `UNSYNC` 状态
5. 再接 `ui_update_todo_state()` 与本地服务 `snapshot`
6. 最后接设置、闹钟、网络动作回调

## 9. 最低验收标准

对接完成后，至少应满足：
- `K1-K4` 能驱动页面跳转
- 雷达连续无人 + 用户连续无操作同时超阈值后进入极简模式
- 雷达恢复有人或用户再操作后退出极简模式
- 温度、湿度、光照能从传感器实时更新到首页
- 时间能从本地服务同步到 UI
- `Todo List` 能从本地服务同步到 UI
- 同步失败时网络页和提示区能展示错误状态
