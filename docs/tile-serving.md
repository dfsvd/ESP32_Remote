# 离线地图瓦片服务

## 架构概览

```
SASPlanet(TMS) → SD卡(/tiles/{z}/{x}/{y}.png)
                        ↓
浏览器 Leaflet ← HTTP chunked ← ESP32 httpd (catchall_handler)
(tms:true)        (Keep-Alive)    (sdcard_fopen + 4KB fread 循环)
```

**核心原则**：不加载整个瓦片到内存，使用 chunked transfer encoding 流式转发。

---

## SD 卡瓦片结构

SASPlanet 导出为 **TMS 格式**（Y 轴翻转），直接存放于 SD 卡根目录：

```
SD卡根目录/tiles/
├── 13/
│   ├── 2342/
│   │   ├── 5054.png
│   │   └── ...
│   ├── 2343/
│   │   ├── 5057.png
│   │   └── ...
│   └── ...
├── 14/
├── 15/
├── 16/
└── 17/
```

每张瓦片为 PNG 格式，单文件 **20-150KB** 不等（取决于地图内容与色彩深度）。

---

## 服务端 (ESP32)

### catchall_handler (`rc_wf.c`)

HTTP 404 错误处理器，拦截所有未匹配的路由。当 URI 匹配 `/tiles/{z}/{x}/{y}.png` 且 SD 卡已挂载时，执行瓦片读取。

**流程**：

```
浏览器请求 /tiles/13/2343/5057.png
  ↓
httpd_uri 无匹配 → 触发 404 错误处理器
  ↓
catchall_handler 解析 z=13, x=2343, y=5057
  ↓
sd_path = "/tiles/13/2343/5057.png"
  ↓
sdcard_fopen("/tiles/13/2343/5057.png")          [重试 5 次, rc_sdcard层封装]
  ↓
malloc(4096) heap buffer                          [不占栈空间]
  ↓
while (fread(buf, 1, 4096, f) > 0)               [SDMMC 1-bit multi-block]
    httpd_resp_send_chunk(req, buf, n)
  ↓
ferror? → 不发送 chunked 终结标记               [浏览器丢弃损坏数据]
正常 → httpd_resp_send_chunk(req, NULL, 0)     [正常终结]
```

**关键设计决策**：

| 决策 | 理由 |
|------|------|
| `malloc(4096)` 而非栈上数组 | 栈上 2KB 即导致 HTTP server 栈溢出 (默认 4KB)，4KB 堆分配安全 |
| 不重试 `fread` 错误 | SPI 读取断裂后文件指针状态未定义，重试导致 PNG 数据静默损坏 |
| `ferror` 时不发送 `0\r\n\r\n` | 浏览器收不到 chunked 终结标记，判定响应不完整 → 丢弃 → Leaflet 自动重试 |
| `Connection: keep-alive` | lwIP TCP 建连开销大 (50-100ms/次)，复用连接可节省大量握手时间 |
| `Keep-Alive: timeout=5, max=100` | 允许单个 TCP 连接最多复用 100 次，5s 空闲超时 |
| `core_id = 0` | 与 Core 1 的 ADC (20kHz) / CRSF 任务物理隔离，减少 APB 总线争用 |
| `stack_size = 8192` | `sdcard_fopen → FATFS → SDMMC` 调用链深，默认 4KB 不够 |
| 通过 `sdcard_fopen()` 而非裸 `fopen` | 统一路径拼接、集中安全边界、上层不耦合挂载点 |

---

## 客户端 (Leaflet)

### MapPanel.vue

```js
tileLayer = L.tileLayer('http://192.168.4.1/tiles/{z}/{x}/{y}.png', {
    maxZoom: 17,
    keepBuffer: 5,        // 拖动时保留更多离屏瓦片
    tms: true,             // SASPlanet TMS 格式 (Y 轴与 XYZ 相反)
}).addTo(map)
```

**`tms: true` 的作用**：Leaflet 请求瓦片时，Y 坐标自动翻转（TMS Y = 2^z - 1 - XYZ Y），使请求路径与 SD 卡上的 TMS 文件名一致。

### 加载失败处理

- SDMMC 读取断裂时，ESP32 返回不完整的 chunked 响应
- 浏览器等待 `0\r\n\r\n` 超时 → 判定瓦片加载失败 → 显示灰块
- Leaflet 在用户拖动/缩放时会重新请求失败的瓦片

---

## 瓦片准备 (SASPlanet)

1. SASPlanet 框选目标区域
2. 导出格式选择 **TMS 瓦片**（不选 XYZ / OSM）
3. 缩放级别推荐 **13-17**
4. 导出目录选择 SD 卡根目录，确保生成 `tiles/{z}/{x}/{y}.png`

---

## 性能优化历史

| 日期 | 改动 | 效果 |
|------|------|------|
| v1 | `sdcard_read_file(malloc 全文件)` | 20KB 小瓦片可用 |
| v2 | 删除内嵌 flash 瓦片，纯 SD 卡读取 | 释放 3MB flash |
| v3 | chunked 流式 + `malloc(4096)` | 支持 150KB 大瓦片 |
| v4 | `core_id = 0` 核心隔离 | 减少 ADC 干扰 SPI |
| v5 | `stack_size = 8192` | 修复栈溢出崩溃 |
| v6 | `Connection: keep-alive` | 减少 TCP 建连开销 |
| v7 | SPI 4线 → SDMMC 1-bit 3线 | 释放 11/12/13 给音频，省 1 个 GPIO |

---

## 相关文件

| 文件 | 角色 |
|------|------|
| `ESP32/components/RC/src/rc_wf.c` | catchall_handler 瓦片服务入口 |
| `ESP32/components/RC/src/rc_sdcard.c` | SD 卡挂载与 FATFS 操作 |
| `ESP32/components/RC/include/rc_sdcard.h` | SDMMC 引脚定义 (`SDMMC_CLK=47` 等) + API 声明 |
| `web/src/components/MapPanel.vue` | Leaflet 地图组件 |
| `web/src/composables/useRCState.js` | 模拟 GPS 数据生成 |
| `ESP32/tools/download_tiles.py` | 在线瓦片下载工具 (已由 SASPlanet 替代) |
