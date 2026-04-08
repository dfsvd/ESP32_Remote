# FPV 遥控器 Web 配置界面

一个运行在 **ESP32** 上的 FPV 遥控器 Web 配置页面。基于 Vue 3 + Vite 构建，通过 WebSocket 与 ESP32 实时通信，提供 18 通道数据可视化、双摇杆显示、通道校准等功能。

## 功能特性

- **18 通道实时显示** — 每个通道以百分比滑块形式展示校准后的值（-100% ~ 100%）
- **双向滑块交互** — 蓝色百分比滑块不仅展示数据，还可直接拖动来反向设置通道原始值
- **通道校准** — 每个通道支持独立设置 Min / Mid / Max 校准值，展开面板后可手动输入或一键捕获当前值
- **原始值滑块** — 展开通道详情后，可通过原始值滑块（1000-2000）直接调整通道值
- **双摇杆可视化** — 左摇杆显示油门/偏航（CH3/CH4），右摇杆显示俯仰/横滚（CH1/CH2），采用 Mode 2 AETR 标准映射
- **WebSocket 实时通信** — 与 ESP32 建立 WebSocket 连接，接收实时通道数据
- **自动重连** — WebSocket 断开后每 3 秒自动尝试重新连接
- **一键保存校准** — 将所有通道的校准数据打包发送给 ESP32

## 技术栈

| 类别 | 技术 |
|------|------|
| 前端框架 | [Vue 3](https://vuejs.org/)（Composition API + `<script setup>`） |
| 构建工具 | [Vite](https://vitejs.dev/) |
| 语言 | JavaScript (ES6+) |
| 通信协议 | WebSocket（原生 API） |
| 目标硬件 | ESP32（将构建产物部署到 SPIFFS / LittleFS） |

## 快速开始

### 安装依赖

```bash
npm install
```

### 开发模式运行

```bash
npm run dev
```

启动后访问 `http://localhost:5173`，页面会尝试连接 `ws://localhost/ws`（开发时 ESP32 未连接，WebSocket 会持续重连，不影响 UI 调试）。

### 构建生产版本

```bash
npm run build
```

生成的 `dist/` 目录包含所有静态资源，可直接部署到 ESP32 的文件系统中。

### 预览生产构建

```bash
npm run preview
```

## 部署到 ESP32

1. 执行 `npm run build` 生成 `dist/` 目录
2. 将 `dist/` 中的文件上传到 ESP32 的 SPIFFS 或 LittleFS 文件系统
3. ESP32 端需运行一个 Web 服务器提供静态文件服务，并在 `/ws` 路径上提供 WebSocket 端点
4. 浏览器访问 ESP32 的 IP 地址即可打开配置页面

## 项目结构

```
rc/
├── index.html                          # HTML 入口文件
├── package.json                        # 项目配置与依赖声明
├── vite.config.js                      # Vite 构建配置
├── public/
│   ├── favicon.svg                     # 网站图标
│   └── icons.svg                       # SVG 图标集
└── src/
    ├── main.js                         # Vue 应用入口
    ├── App.vue                         # 根组件（布局 + 状态管理）
    ├── style.css                       # 全局基础样式
    └── components/
        ├── AuxChannelSlider.vue        # 通道滑块组件（校准 + 双向交互）
        ├── Joystick.vue                # 摇杆可视化组件
        └── WebSocketConnection.vue     # WebSocket 连接管理组件
```

## 组件详解

### `index.html`

应用的 HTML 入口文件。包含一个 `<div id="app">` 挂载点和对 `src/main.js` 的模块引用。

---

### `src/main.js`

Vue 应用启动脚本。导入根组件 `App.vue` 和全局样式 `style.css`，创建 Vue 实例并挂载到 `#app` 元素。

---

### `src/style.css`

全局基础样式表，来源于 Vite 模板的默认样式。应用的主要样式定义在各 `.vue` 组件的 `<style scoped>` 中，尤其是 `App.vue` 中定义了所有 CSS 变量和整体布局。

---

### `src/App.vue` — 根组件

**职责：** 整体布局管理 + 全局状态中心 + 数据分发枢纽

#### 布局结构

采用 CSS Grid 将页面分为三个区域：

| 区域 | Grid Area | 内容 |
|------|-----------|------|
| 顶栏 | `header` | 保存按钮 |
| 侧边栏 | `sidebar` | 18 个通道滑块（可滚动） |
| 主区域 | `joysticks` | 左/右虚拟摇杆 |

```
┌──────────────┬──────────┐
│   header     │          │
├──────────────┤ sidebar  │
│              │ (通道列表)│
│  joysticks   │          │
│  (左/右摇杆) │          │
└──────────────┴──────────┘
```

#### 状态管理

- **`channels`** — 响应式数组，包含 18 个通道对象，每个对象结构为：
  ```js
  {
    id: Number,           // 通道编号 1-18
    rawValue: Number,     // 原始值 1000-2000
    cal: {                // 校准参数
      min: Number,        // 最小值（默认 1000）
      mid: Number,        // 中间值（默认 1500）
      max: Number         // 最大值（默认 2000）
    }
  }
  ```

#### 数据流

1. **接收数据：** `WebSocketConnection` 组件接收到 ESP32 发来的数据后，触发 `data` 事件 → `handleWebSocketData()` 解析逗号分隔的值 → 更新 `channels[].rawValue`
2. **摇杆计算：** `leftStick` 和 `rightStick` 计算属性从 `channels` 中读取 CH1-CH4 的校准后百分比值，传递给 `Joystick` 组件
3. **保存校准：** 点击"保存"按钮 → `sendCalibrationData()` 将所有通道的 `cal` 数据格式化为字符串 → 通过 WebSocket 发送给 ESP32

#### 摇杆映射（Mode 2 AETR）

| 摇杆 | X 轴 | Y 轴 |
|------|------|------|
| 右摇杆 | CH1 横滚 (Roll) | CH2 俯仰 (Pitch) |
| 左摇杆 | CH4 偏航 (Yaw) | CH3 油门 (Throttle) |

---

### `src/components/AuxChannelSlider.vue` — 通道滑块组件

**职责：** 单个通道的数据展示 + 校准配置 + 双向值调整

#### Props

| 属性 | 类型 | 说明 |
|------|------|------|
| `channelId` | `Number` | 通道编号 |
| `rawValue` | `Number` | 原始通道值（1000-2000），支持 `v-model` 双向绑定 |
| `cal` | `Object` | 校准参数对象 `{ min, mid, max }` |

#### 核心逻辑

**正向计算（原始值 → 百分比）：**

`calibratedValue` 计算属性将 `rawValue`（1000-2000）根据校准参数转换为百分比（-100 ~ 100）：

```
rawValue < mid → percentage = -100 × (mid - rawValue) / (mid - min)
rawValue ≥ mid → percentage =  100 × (rawValue - mid) / (max - mid)
```

**反向计算（百分比 → 原始值）：**

`handlePercentageInput()` 函数在用户拖动蓝色百分比滑块时触发，将百分比反推为原始值：

```
percent < 0 → rawValue = mid + (percent / 100) × (mid - min)
percent > 0 → rawValue = mid + (percent / 100) × (max - mid)
percent = 0 → rawValue = mid
```

#### UI 结构

- **折叠状态（默认）：** 显示通道标签 + 蓝色百分比滑块 + 百分比数值 + 展开按钮
- **展开状态：** 额外显示原始值、原始值滑块（1000-2000）、Min/Mid/Max 校准输入框和 Set 按钮

#### 数据流示意

```
拖动百分比滑块 ──→ handlePercentageInput() ──→ emit('update:rawValue') ──→ App.vue
拖动原始值滑块 ──→ 直接 emit('update:rawValue') ──→ App.vue
ESP32 数据    ──→ App.vue 更新 rawValue ──→ calibratedValue 重新计算 ──→ 滑块位置更新
```

---

### `src/components/Joystick.vue` — 摇杆可视化组件

**职责：** 以圆形摇杆的形式直观展示两个轴向的通道数据

#### Props

| 属性 | 类型 | 说明 |
|------|------|------|
| `x` | `Number` | X 轴值（-100 ~ 100） |
| `y` | `Number` | Y 轴值（-100 ~ 100） |
| `label` | `String` | 摇杆标签文字 |

#### 渲染逻辑

- 摇杆底座为 150×150px 的圆形区域
- 摇杆头为 50×50px 的发光圆球
- `stickStyle` 计算属性将 `x`/`y` 坐标映射到 CSS `left`/`top` 百分比值
- 自动将超出圆形边界的坐标约束到圆的边缘（向量归一化）
- 纯展示组件，不发射任何事件

---

### `src/components/WebSocketConnection.vue` — WebSocket 连接管理

**职责：** 管理与 ESP32 的 WebSocket 连接，提供数据收发能力

#### 特点

- **无界面组件** — 没有 `<template>`，纯逻辑组件
- **自动连接** — 组件挂载时自动连接 `ws://{当前主机}/ws`
- **自动重连** — 连接断开后每 3 秒自动重试
- **生命周期管理** — 组件卸载时自动清理定时器和关闭连接

#### 事件

| 事件 | 参数 | 说明 |
|------|------|------|
| `data` | `String` | 收到 WebSocket 消息时触发 |
| `status` | `Boolean` | 连接状态变化时触发 |

#### 暴露方法

| 方法 | 参数 | 说明 |
|------|------|------|
| `sendData` | `String` | 通过 WebSocket 发送数据 |

## 通信协议

### 接收数据格式（ESP32 → 页面）

ESP32 通过 WebSocket 发送逗号分隔的 18 通道原始值，每行一条：

```
1500,1500,1000,1500,1500,1500,1500,1500,1500,1500,1500,1500,1500,1500,1500,1500,1500,1500
```

每个值的范围为 `1000 ~ 2000`，分别对应 CH1 ~ CH18。

### 发送校准指令（页面 → ESP32）

点击"保存"按钮时，发送格式为：

```
C:1,1000,1500,2000;2,1000,1500,2000;3,1000,1500,2000;...
```

格式说明：`C:` 前缀 + 每个通道 `通道号,min,mid,max`，通道间以 `;` 分隔。

## CSS 变量

应用在 `App.vue` 中定义了全局 CSS 变量，组件通过这些变量保持视觉一致性：

| 变量 | 默认值 | 用途 |
|------|--------|------|
| `--bg-color` | `#1e1e1e` | 页面背景色 |
| `--panel-bg-color` | `#242424` | 面板背景色 |
| `--border-color` | `#333` | 边框/分割线颜色 |
| `--text-color` | `#f0f0f5` | 主要文字颜色 |
| `--text-secondary-color` | `#a0a0a5` | 次要文字颜色 |
| `--accent-color` | `#00bfff` | 强调色（蓝色） |
| `--glow-color` | `rgba(0,191,255,0.5)` | 发光效果颜色 |
| `--header-height` | `60px` | 顶栏高度 |

## 开发说明

- 开发时 WebSocket 连接会失败（因为没有 ESP32），控制台会持续输出重连日志，这是正常行为
- `App.vue` 中注释掉的 `dataSimulator` 代码可以取消注释来模拟随机通道数据，方便 UI 调试
- 所有组件使用 `<style scoped>` 确保样式隔离
- 使用 Vue 3 Composition API (`<script setup>`) 编写，无 Options API 代码

## License

MIT
