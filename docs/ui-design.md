# 智能闹钟 UI 设计文档

## 1. 文档范围
- 屏幕规格：`240x320` 竖屏
- 文档内容：静态页面结构、页面元素、按键功能标注
- 页面集合：正常显示首页、极简显示页、设置页、闹钟页（列表态）、闹钟页（新建选择页）、网络页、关机确认页

---

## 2. 页面总览（静态缩略）

### 2.1 正常显示首页
<div style="width:240px;height:320px;background:#000;color:#fff;border:1px solid #555;box-sizing:border-box;padding:8px;font-family:monospace;display:flex;flex-direction:column;gap:6px;">
  <div style="display:flex;gap:6px;height:76px;">
    <div style="flex:1;border:1px solid #444;padding:4px;display:flex;flex-direction:column;justify-content:center;">
      <div style="font-size:11px;color:#79a7ff;">2026-05-15 FRI</div>
      <div style="font-size:36px;line-height:1;">19:30<span style="font-size:18px;">:08</span></div>
    </div>
    <div style="width:74px;border:1px solid #444;padding:4px;display:flex;flex-direction:column;justify-content:center;gap:4px;font-size:12px;">
      <div style="display:flex;justify-content:space-between;"><span>声音</span><span>ON</span></div>
      <div style="display:flex;justify-content:space-between;"><span>语音</span><span>OFF</span></div>
    </div>
  </div>
  <div style="height:24px;border:1px solid #444;font-size:12px;display:flex;align-items:center;justify-content:space-around;">
    <span style="color:#ffab40;">温 26C</span><span style="color:#86d3ff;">湿 58%</span><span style="color:#ffd54f;">光 320</span>
  </div>
  <div style="height:24px;border:1px solid #444;padding:4px;font-size:12px;display:flex;align-items:center;gap:6px;">
    <span style="color:#bdbdbd;">Tips</span>
    <span>环境光偏低，建议开灯</span>
  </div>
  <div style="height:24px;border:1px solid #444;padding:4px;display:flex;justify-content:space-between;align-items:center;">
    <div style="display:flex;align-items:center;gap:6px;"><span style="color:#bdbdbd;font-size:12px;">闹钟</span><span style="font-size:16px;line-height:1;">07:30</span></div>
    <div style="font-size:11px;color:#bdbdbd;">REPEAT</div>
  </div>
  <div style="flex:1;border:1px solid #444;padding:4px;font-size:12px;display:flex;flex-direction:column;gap:2px;">
    <div style="color:#bdbdbd;">Todo List</div>
    <div>1. 取快递</div>
    <div>2. 提交周报</div>
    <div style="color:#9e9e9e;">+4</div>
  </div>
  <div style="height:22px;border:1px solid #444;font-size:12px;display:grid;grid-template-columns:repeat(4,1fr);text-align:center;align-items:center;">
    <span>设置</span><span>闹钟</span><span>网络</span><span>关机</span>
  </div>
</div>

### 2.2 极简显示页
<div style="width:240px;height:320px;background:#000;color:#fff;border:1px solid #555;box-sizing:border-box;font-family:monospace;display:flex;align-items:center;justify-content:center;">
  <div style="font-size:56px;line-height:1;">19:30</div>
</div>

### 2.3 设置页
<div style="width:240px;height:320px;background:#000;color:#fff;border:1px solid #555;box-sizing:border-box;padding:8px;font-family:monospace;display:flex;flex-direction:column;gap:6px;">
  <div style="height:28px;border:1px solid #444;display:flex;align-items:center;justify-content:center;font-size:18px;">设置</div>
  <div style="flex:1;border:1px solid #444;padding:4px;display:flex;flex-direction:column;gap:4px;font-size:14px;">
    <div style="border:1px solid #777;padding:4px;display:flex;justify-content:space-between;background:#111;"><span>SOUND</span><span>ON</span></div>
    <div style="border:1px solid #444;padding:4px;display:flex;justify-content:space-between;"><span>AUDIO</span><span>OFF</span></div>
    <div style="border:1px solid #444;padding:4px;display:flex;justify-content:space-between;"><span>VOLUME</span><span>6</span></div>
    <div style="border:1px solid #444;padding:4px;display:flex;justify-content:space-between;"><span>ENV SAMPLE</span><span>5s</span></div>
    <div style="border:1px solid #444;padding:4px;display:flex;justify-content:space-between;"><span>REPEAT</span><span>2</span></div>
  </div>
  <div style="height:22px;border:1px solid #444;font-size:12px;display:grid;grid-template-columns:repeat(4,1fr);text-align:center;align-items:center;">
    <span>上移</span><span>下移</span><span>选择</span><span>返回</span>
  </div>
</div>

### 2.4 闹钟页（列表态）
<div style="width:240px;height:320px;background:#000;color:#fff;border:1px solid #555;box-sizing:border-box;padding:8px;font-family:monospace;display:flex;flex-direction:column;gap:6px;">
  <div style="height:28px;border:1px solid #444;display:flex;align-items:center;justify-content:center;font-size:18px;">闹钟</div>
  <div style="flex:1;display:grid;grid-template-columns:110px 1fr;gap:6px;">
    <div style="border:1px solid #444;padding:4px;display:flex;flex-direction:column;gap:3px;font-size:13px;">
      <div style="border:1px solid #777;background:#111;padding:3px;">+ 新建闹钟</div>
      <div style="border:1px solid #444;padding:3px;">07:30 MON WED FRI</div>
      <div style="border:1px solid #444;padding:3px;">08:00 DAILY</div>
      <div style="border:1px solid #444;padding:3px;">20:15 ONCE</div>
    </div>
    <div style="border:1px solid #444;padding:6px;display:flex;flex-direction:column;justify-content:space-between;">
      <div style="font-size:12px;color:#bdbdbd;">详情</div>
      <div style="font-size:28px;line-height:1;">07:30</div>
      <div style="font-size:12px;">MON WED FRI</div>
      <div style="font-size:12px;">REPEAT / ENABLED</div>
    </div>
  </div>
  <div style="height:22px;border:1px solid #444;font-size:12px;display:grid;grid-template-columns:repeat(4,1fr);text-align:center;align-items:center;">
    <span>上移</span><span>下移</span><span>选择</span><span>返回</span>
  </div>
</div>

### 2.5 闹钟页（新建闹钟选择页）
<div style="width:240px;height:320px;background:#000;color:#fff;border:1px solid #555;box-sizing:border-box;padding:8px;font-family:monospace;display:flex;flex-direction:column;gap:6px;">
  <div style="height:28px;border:1px solid #444;display:flex;align-items:center;justify-content:center;font-size:18px;">闹钟新建</div>
  <div style="flex:1;border:1px solid #444;padding:4px;display:flex;flex-direction:column;gap:3px;font-size:13px;">
    <div style="border:1px solid #777;background:#111;padding:3px;display:flex;justify-content:space-between;"><span>时</span><span>08</span></div>
    <div style="border:1px solid #444;padding:3px;display:flex;justify-content:space-between;"><span>分</span><span>00</span></div>
    <div style="border:1px solid #444;padding:3px;display:flex;justify-content:space-between;"><span>秒</span><span>00</span></div>
    <div style="border:1px solid #444;padding:3px;display:flex;justify-content:space-between;"><span>星期方案</span><span>工作日</span></div>
    <div style="border:1px solid #444;padding:3px;display:flex;justify-content:space-between;"><span>重复</span><span>ON</span></div>
    <div style="border:1px solid #444;padding:3px;display:flex;justify-content:space-between;"><span>启用</span><span>ON</span></div>
  </div>
  <div style="height:22px;border:1px solid #444;font-size:12px;display:grid;grid-template-columns:repeat(4,1fr);text-align:center;align-items:center;">
    <span>上移</span><span>下移</span><span>选择</span><span>返回</span>
  </div>
</div>

### 2.6 网络页（模块选择态）
<div style="width:240px;height:320px;background:#000;color:#fff;border:1px solid #555;box-sizing:border-box;padding:8px;font-family:monospace;display:flex;flex-direction:column;gap:6px;">
  <div style="height:28px;border:1px solid #444;display:flex;align-items:center;justify-content:center;font-size:18px;">网络</div>
  <div style="height:250px;display:flex;flex-direction:column;gap:6px;">
    <div style="height:108px;display:flex;gap:6px;">
      <div style="width:109px;border:1px solid #777;background:#111;padding:3px;font-size:11px;display:flex;flex-direction:column;gap:2px;line-height:1.1;">
        <div style="color:#bdbdbd;">WiFi状态</div>
        <div>Connected</div>
        <div>Home-2.4G</div>
        <div>192.168.1.72</div>
      </div>
      <div style="width:109px;border:1px solid #444;padding:3px;font-size:11px;display:flex;flex-direction:column;gap:2px;">
        <div style="color:#bdbdbd;">扫描结果</div>
        <div style="border:1px solid #444;padding:2px;">Home-2.4G</div>
        <div style="border:1px solid #444;padding:2px;">Office-5G</div>
        <div style="border:1px solid #444;padding:2px;">Lab-AP01</div>
      </div>
    </div>
    <div style="height:136px;display:flex;gap:6px;">
      <div style="width:109px;border:1px solid #444;padding:4px;font-size:12px;display:flex;flex-direction:column;gap:3px;">
        <div style="color:#bdbdbd;">动作</div>
        <div style="border:1px solid #444;padding:2px;">扫描</div>
        <div style="border:1px solid #444;padding:2px;">断开</div>
        <div style="border:1px solid #444;padding:2px;">校时</div>
        <div style="border:1px solid #444;padding:2px;">同步Todo</div>
      </div>
      <div style="width:109px;border:1px solid #444;padding:4px;font-size:11px;display:flex;flex-direction:column;gap:3px;">
        <div style="color:#bdbdbd;">同步状态</div>
        <div>TIME OK</div>
        <div>TODO OK</div>
        <div>19:25:10</div>
      </div>
    </div>
  </div>
  <div style="height:22px;border:1px solid #444;font-size:12px;display:grid;grid-template-columns:repeat(4,1fr);text-align:center;align-items:center;">
    <span>上移</span><span>下移</span><span>选择</span><span>返回</span>
  </div>
</div>

### 2.7 关机确认页
<div style="width:240px;height:320px;background:#000;color:#fff;border:1px solid #555;box-sizing:border-box;padding:8px;font-family:monospace;display:flex;flex-direction:column;gap:6px;">
  <div style="height:28px;border:1px solid #444;display:flex;align-items:center;justify-content:center;font-size:18px;">关机确认</div>
  <div style="flex:1;border:1px solid #444;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:10px;">
    <div style="font-size:28px;">确认关机？</div>
    <div style="font-size:12px;color:#bdbdbd;">再次确认后关闭系统</div>
  </div>
  <div style="height:22px;border:1px solid #444;font-size:12px;display:grid;grid-template-columns:repeat(4,1fr);text-align:center;align-items:center;">
    <span>返回</span><span>-</span><span>-</span><span>确认</span>
  </div>
</div>

---

## 3. 页面元素说明

### 3.1 正常显示首页
- 时间区：日期、星期、时分秒
- 状态区：声音、语音（`ON/OFF`）
- 环境区：温度、湿度、光照
- Tips 区：单条提示语
- 闹钟区：最近闹钟时间、重复标记
- Todo 区：固定 2 条，超出显示 `+N`
- 底栏：`设置 / 闹钟 / 网络 / 关机`

### 3.2 极简显示页
- 居中显示当前时间

### 3.3 设置页
- 列表项：`SOUND`、`AUDIO`、`VOLUME`、`ENV SAMPLE`、`REPEAT`

### 3.4 闹钟页
- 列表区：`+ 新建闹钟` 与已有闹钟
- 详情区：当前选中项摘要
- 编辑字段：时、分、秒、星期方案、重复、启用
- 删除确认：确认文案与当前闹钟摘要

### 3.5 闹钟页（新建闹钟选择页）
- 字段列表：时、分、秒、星期方案、重复、启用
- 单字段高亮：当前可调整项

### 3.6 网络页
- 模块：`WiFi状态`、`动作`、`扫描结果`、`同步状态`
- 扫描结果：静态展示 3 条
- 动作项：`扫描`、`断开`、`校时`、`同步Todo`

### 3.7 关机确认页
- 关机确认文案

---

## 4. 按键功能标注

### 4.1 首页
| 实体按键 | 功能 |
| --- | --- |
| K1 | 设置 |
| K2 | 闹钟 |
| K3 | 网络 |
| K4 | 关机 |

### 4.2 设置页 / 闹钟页 / 网络页（统一）
| 实体按键 | 功能 |
| --- | --- |
| K1 | 上移 |
| K2 | 下移 |
| K3 | 选择 |
| K4 | 返回 |

### 4.3 关机确认页
| 实体按键 | 功能 |
| --- | --- |
| K1 | 返回 |
| K2 | - |
| K3 | - |
| K4 | 确认 |

---

## 5. 默认状态与状态转移

### 5.1 正常显示首页
- 默认状态：正常显示首页

```mermaid
stateDiagram-v2
    [*] --> 正常显示首页
    正常显示首页 --> 设置页 : K1
    正常显示首页 --> 闹钟页（列表态） : K2
    正常显示首页 --> 网络页 : K3
    正常显示首页 --> 关机确认页 : K4
```

### 5.2 极简显示页
- 默认状态：居中时间显示

```mermaid
stateDiagram-v2
    [*] --> 极简显示页
    极简显示页 --> 正常显示首页 : 退出极简
```

### 5.3 设置页
- 默认状态：浏览态
- 默认焦点控件：`SOUND`
- 焦点顺序：`SOUND -> AUDIO -> VOLUME -> ENV SAMPLE -> REPEAT -> SOUND`

```mermaid
stateDiagram-v2
    [*] --> 浏览态
    浏览态 --> 编辑态 : K3
    编辑态 --> 浏览态 : K3
    编辑态 --> 浏览态 : K4
    浏览态 --> 正常显示首页 : K4
```

### 5.4 闹钟页
- 默认状态：列表态
- 默认焦点控件：`+ 新建闹钟`

```mermaid
stateDiagram-v2
    [*] --> 列表态
    列表态 --> 新建选择页 : 选中 + 新建闹钟 / K3
    列表态 --> 动作态 : 选中 已有闹钟 / K3
    动作态 --> 新建选择页 : 选中 编辑 / K3
    动作态 --> 列表态 : 选中 启用或停用 / K3
    动作态 --> 删除确认态 : 选中 删除 / K3
    动作态 --> 列表态 : K4
    新建选择页 --> 列表态 : 保存完成
    新建选择页 --> 列表态 : K4
    删除确认态 --> 动作态 : K1
    删除确认态 --> 列表态 : K4
    列表态 --> 正常显示首页 : K4
```

### 5.5 网络页
- 默认状态：模块选择态
- 默认焦点控件：`WiFi状态`
- 模块顺序：`WiFi状态 -> 动作 -> 扫描结果 -> 同步状态 -> WiFi状态`

```mermaid
stateDiagram-v2
    [*] --> WiFi状态焦点
    WiFi状态焦点 --> 动作焦点 : K2
    动作焦点 --> 扫描结果焦点 : K2
    扫描结果焦点 --> 同步状态焦点 : K2
    同步状态焦点 --> WiFi状态焦点 : K2

    WiFi状态焦点 --> 同步状态焦点 : K1
    同步状态焦点 --> 扫描结果焦点 : K1
    扫描结果焦点 --> 动作焦点 : K1
    动作焦点 --> WiFi状态焦点 : K1

    动作焦点 --> 动作模块内选择态 : K3
    扫描结果焦点 --> 扫描结果模块内选择态 : K3

    动作模块内选择态 --> 动作焦点 : K4
    扫描结果模块内选择态 --> 扫描结果焦点 : K4

    WiFi状态焦点 --> 正常显示首页 : K4
    动作焦点 --> 正常显示首页 : K4
    扫描结果焦点 --> 正常显示首页 : K4
    同步状态焦点 --> 正常显示首页 : K4
```

### 5.6 关机确认页
- 默认状态：关机确认页

```mermaid
stateDiagram-v2
    [*] --> 关机确认页
    关机确认页 --> 正常显示首页 : K1
    关机确认页 --> [*] : K4
```

---

## 6. 静态 HTML 组织规范（用于后续 LVGL 转写）
- 每个页面以 `240x320` 单容器表示
- 页面分为：标题区、内容区、按键标注区
- 内容区内部使用固定像素块布局，不依赖脚本、不依赖外部 CSS
- 颜色与字号使用明确常量，不使用动态主题切换
- 所有预览均为静态 HTML，无 JS 交互
