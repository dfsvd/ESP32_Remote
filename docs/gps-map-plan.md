# GPS 地图功能 — 方案评估与执行计划

## 背景

目前系统已具备完整的 GPS 数据链路：

1. **数据源**：CRSF 遥测帧 (0x02) → `crsf_state_t.telemetry.gps` (`rc_crsf.c:690`)
2. **WebSocket 推送**：`rc_wf.c` 的 `ws_broadcast_task` 以 20Hz 频率广播 `TELEMETRY:{"gps":{"lat":223456789,"lon":114123456,...}}` (`rc_wf.c:239-263`)
3. **前端接收**：`useRCState.js` 中 `normalizeTelemetry()` 已解析 GPS 数据并做精度归一化（纬度/经度 `/1e7`）
4. **当前展示**：`TelemetryPanel.vue` 以文本形式显示经纬度、高度、速度等

目标：在 Web 前端增加地图，显示飞机当前位置 + 飞行轨迹。

---

## 方案一：在线版本 ⭐ 推荐

### 核心思路

**不需要 ESP32 做代理或中继。** 手机/笔记本连接遥控器 WiFi AP 后，浏览器直接通过手机蜂窝网络加载地图瓦片。

现代手机的行为：连接到一个没有互联网的 WiFi 热点时，**浏览器仍可以使用蜂窝数据**访问互联网。遥控器已经实现了 Captive Portal DNS 重定向（`rc_wf.c:48-161` 的 DNS 任务），手机不会因"无网络"而断开 WiFi。

所以数据流是双通道：
```
GPS 位置: ESP32 (WebSocket) ────WiFi AP────→ 浏览器 (192.168.4.1/ws)
地图瓦片: OpenStreetMap/CDN    ──蜂窝数据──→ 浏览器 (直接 HTTP)
```

### 对 ESP32 固件的改动

**零改动（Phase 1-4）。** 不需要：
- 开启 STA 模式
- 配置代理
- 修改 WebSocket 协议
- 增加 filesystem 支持
- 增加任何硬件

### 对 Web 前端的改动

1. **新增 npm 依赖**：`leaflet`
2. **新增组件**：`MapPanel.vue` / `MapPanel_en.vue`
3. **新增导航 Tab**：在 `Main_en.vue` / `Main_zh.vue` 的 `navItems` 中增加 `map` 选项卡
4. **路由不变**（视图切换由 `currentTab` 控制，不涉及 Hash Router）

### 优势

- 实现极简单（只改前端）
- 地图无限区域、无限缩放级别
- 地图永远最新
- 不需要额外硬件
- 手机不需要装任何 App

### 劣势

- 需要蜂窝网络覆盖（飞场无信号则无法加载地图）
- OSM 瓦片在国内可能加载慢，需备选高德/天地图源

### 国内地图源替代方案

| 地图源 | URL 模板 | 要求 |
|--------|----------|------|
| 天地图 | `https://t0.tianditu.gov.cn/vec_w/wmts?SERVICE=WMTS&REQUEST=GetTile&VERSION=1.0.0&LAYER=vec&STYLE=default&TILEMATRIXSET=w&FORMAT=tiles&TILEMATRIX={z}&TILEROW={y}&TILECOL={x}&tk=YOUR_KEY` | 免费申请 Key |
| 高德地图 | `https://webrd0{s}.is.autonavi.com/appmaptile?lang=zh_cn&size=1&scale=1&style=8&x={x}&y={y}&z={z}` | 通常可直接使用 |
| ArcGIS 卫星 | `https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}` | 卫星图，可直接使用 |

---

## 方案二：离线版本（TF 卡）

### 需要做的事情

1. **硬件**：需确认 PCB 是否有 TF 卡槽可用引脚。
2. **ESP32 固件改动**：
   - 添加 `esp_driver_sdspi` 驱动 + FATFS VFS 挂载
   - HTTP server 中增加 `/tiles/{z}/{x}/{y}.png` 路由，从 SD 卡读取
   - **建议开启 PSRAM**
3. **离线地图制作**：用户需用 MOBAC 等工具下载瓦片到 TF 卡

### 劣势

- 硬件不确定（需确认 PCB 是否有 TF 卡设计）
- 开发量大（固件大面积改动）
- 使用门槛高（用户需制作 TF 卡）
- 地图区域有限、可能过时
- 当前无 PSRAM，RAM 仅 512KB

---

## 推荐方案：在线版本（方案一）

| 维度 | 在线 | 离线 |
|------|------|------|
| 开发成本 | 低（只改前端） | 高（硬件+固件+前端） |
| 用户体验 | 插电即用 | 需制作 TF 卡 |
| 地图覆盖 | 全球 | 有限区域 |
| 维护成本 | 零 | 需更新地图 |
| 硬件依赖 | 无 | 需 SD 卡槽 |

---

## 执行计划（分阶段增量推进）

### Phase 1：模拟 GPS 数据

> **目标**：在 `useRCState.js` 中增加模拟 GPS 数据模块，完全解耦高频头。

- 增加 `enableMockTelemetry()` 函数
- 初始位置：深圳某地 (lat=22.5, lon=114.0)
- 模拟绕圈/8字飞行，每秒更新，每次 ~10m
- 通过 `?mock=true` 激活
- 不影响正式 WebSocket 连接逻辑

### Phase 2：前端地图展示

> **目标**：Leaflet 地图 + 实时飞机位置。不依赖固件。

1. `npm install leaflet`
2. 创建 `MapPanel.vue` / `MapPanel_en.vue`
3. 瓦片源（默认高德，可切换）
4. 飞机标记（lat/lon → Leaflet marker）
5. 自动跟随模式
6. GPS 丢失时显示"等待 GPS 信号"
7. 添加到导航 Tab

### Phase 3：网络兼容（仅在需要时）

如果手机蜂窝无法加载瓦片（DNS 被 captive portal 拦截）：
- `rc_wf.c` DNS 处理：未知域名 rcode=5（REFUSED）而非 NXDOMAIN

### Phase 4：飞行轨迹

- 前端累积 GPS 点数组 → Leaflet Polyline
- 最多 2000 点，自动裁剪旧点
- 轨迹颜色渐变
- 清除按钮

### Phase 5（可选）：笔记本 AP+STA

- ESP32 同时 AP + STA 模式
- 通过 STA 代理瓦片请求

---

## 验证方法

1. **本地开发**：`npm run dev?mock=true` 模拟 GPS 数据测试
2. **手机验证**：连遥控器 WiFi → 地图 Tab → 瓦片加载确认
3. **飞行验证**：飞场实测位置更新 + 轨迹记录
4. **切换测试**：高德/天地图/OSM 不同源效果对比
