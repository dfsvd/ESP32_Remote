# GPS 地图功能 — 实现文档

> 📌 本文档最初是方案评估，记录最终实现供后续维护参考。

## 背景

系统已具备完整的 GPS 数据链路：

1. **数据源**：CRSF 遥测帧 (0x02) → `crsf_state_t.telemetry.gps` (`rc_crsf.c`)
2. **WebSocket 推送**：`rc_wf.c` 的 `ws_broadcast_task` 以 20Hz 广播 `TELEMETRY:{...}`
3. **前端接收**：`useRCState.js` 中 `normalizeTelemetry()` 解析 GPS 数据
4. **地图展示**：`MapPanel.vue` Leaflet 地图 + 实时飞机位置 + 飞行轨迹

---

## 最终架构 (已实现)

### 数据流

```
CRSF 遥测 → WebSocket → useRCState.js → MapPanel.vue (Leaflet)
                                           │
SD卡 SASPlanet TMS 瓦片 ← HTTP chunked ← ESP32 catchall_handler
```

**关键决策**：离线 SD 卡方案。不依赖手机蜂窝网络，ESSP32 自建 WiFi AP 承载 Web UI + 瓦片服务。

### 组件关系

| 组件 | 文件 | 职责 |
|------|------|------|
| 地图面板 | `MapPanel.vue` | Leaflet 地图容器、飞机标记、轨迹、自动跟随 |
| 模拟 GPS | `useRCState.js` | `?mock=true` 激活，在深圳绕 8 字飞行 |
| 瓦片服务 | `rc_wf.c` | catchall_handler chunked 流式发送 |
| SD 卡驱动 | `rc_sdcard.c` | SDMMC 1-bit + FATFS 挂载 TF 卡 |
| 遥测推送 | `rc_wf.c` | WebSocket 广播 CRSF 遥测数据 |

### 离线瓦片服务

详见 [`tile-serving.md`](tile-serving.md)。

核心要点：
- SASPlanet 导出 **TMS 格式**瓦片到 SD 卡 `/tiles/{z}/{x}/{y}.png`
- ESP32 用 `fopen + 4KB chunked 流式发送`，不 `malloc` 整个文件
- Leaflet 配置 `tms: true` 适配 TMS 坐标
- Keep-Alive 减少 TCP 建连开销

---

## 实现过程

### Phase 1：模拟 GPS 数据 ✓

`useRCState.js` 中 `startMockTelemetry()`：
- 深圳中心 (22.6513, 114.0355)，半径 0.05° 8 字飞行
- 120m 高度，~43km/h 速度
- 每 20 步模拟 1 次卫星丢失（GPS 信号测试）
- 通过 `?mock=true` 或 `localStorage` 激活

### Phase 2：前端地图展示 ✓

- `npm install leaflet` → `MapPanel.vue`
- 圆形飞机标记 (`L.circleMarker`)
- 自动跟随 / 居中 / 轨迹开关
- 底部状态栏（卫星数、高度、速度、航向）
- GPS 丢失时显示"等待 GPS 信号"提示

### Phase 3：飞行轨迹 ✓

- `L.polyline` 累积 GPS 点
- 最多 2000 点，自动裁剪
- 清除轨迹按钮

### Phase 4：离线瓦片服务 ✓

| 步骤 | 详情 |
|------|------|
| SD 卡 | SDMMC 1-bit (CLK=47, CMD=48, D0=21) |
| 文件系统 | FATFS, 挂载点 `/sd` |
| 瓦片路径 | `/tiles/{z}/{x}/{y}.png` |
| HTTP 服务 | catchall_handler (404 error handler) |
| 读取方式 | `fopen` + `fread(4KB)` + `httpd_resp_send_chunk` |
| 性能优化 | Core 0 隔离、Keep-Alive、8KB 栈 |

### Phase 5：在线源切换 ✗（已废弃）

最初设计了高德/OSM 等在线瓦片源切换，因以下原因废弃：
- 飞场常无蜂窝信号
- 增加前端复杂度
- 最终决定纯离线方案

---

## 验证方法

1. **本地开发**：`npm run dev && open http://localhost:5174/zh?mock=true`
2. **手机验证**：连 DARWINFPV_WIFI → 地图 Tab → 瓦片加载
3. **飞行验证**：飞场实测位置更新 + 轨迹记录

---

## 相关文档

- [`tile-serving.md`](tile-serving.md) — 离线瓦片服务架构与性能优化
- `MapPanel.vue` — 前端地图实现
- `useRCState.js` — 模拟 GPS 与遥测状态管理
- `rc_wf.c` — catchall_handler + WebSocket 广播
- `rc_sdcard.c` — SD 卡挂载与 FATFS 封装
