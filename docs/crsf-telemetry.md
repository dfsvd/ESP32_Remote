# CRSF 回传数据解析 — 技术文档

## 概述

本文档记录遥控器与飞控之间通过 CRSF 协议接收回传数据（Telemetry）的实现细节、解析方法和当前验收状态。

## 数据流

```
飞控(FC) ←UART/CRSF→ 接收机(RX) ←RF链路→ 高频头(ELRS) ←UART→ ESP32(遥控器)
                                                        ↓
                                              crsf_rx_task 解析帧
                                                        ↓
                                              crsf_telemetry_t
                                                        ↓
                                              main.c 主循环 5s 打印
```

飞控（Betaflight/INAV/Ardupilot）产生回传数据 → 接收机通过 RF 链路转发 → 遥控器 UART 接收 → `crsf_rx_task()` 解析。

## 文件位置

| 文件 | 作用 |
|------|------|
| `components/RC/include/rc_crsf.h` | `crsf_telemetry_t` 结构体定义 |
| `components/RC/src/rc_crsf.c` | `crsf_rx_task()` 帧解析 |
| `main/main.c` | 主循环每 5s 打印回传快照 |

## 当前已解析的所有帧类型

### 1. LINK_STAT (0x14) — 链路状态

*始终在接收，与飞控无关。*

| 字段 | 类型 | 说明 | 验证状态 |
|------|------|------|:-------:|
| RSSI | uint8 | 信号强度 raw (0~255，-128=实际dBm) | ✅ 长期可用 |
| LQ | uint8 | 链路质量 (0~100%) | ✅ |
| SNR | int8 | 信噪比 dB | ✅ |

### 2. ATTITUDE (0x1E) — 姿态

*帧结构：payload = pitch(2) + roll(2) + yaw(2)*  
*单位：rad × 10000，大端序*

| 字段 | 解析代码 | 验证状态 |
|------|---------|:-------:|
| pitch | `(int16_t)((payload[0]<<8) \| payload[1])` | ✅ 已验证 |
| roll | `(int16_t)((payload[2]<<8) \| payload[3])` | ✅ 已验证 |
| yaw | `(int16_t)((payload[4]<<8) \| payload[5])` | ✅ 已验证 |

**角度转换公式：**
```c
角度 = raw_value × 180.0 / 31415.9  // 单位: 度
```

**验证结果：** Betaflight 上位机显示 pitch=44° roll=0.5° yaw=42° ↔ 遥控器日志 p=7714(+44.2°) r=87(+0.5°) y=7714(+44.2°) 完全吻合。

### 3. BATTERY (0x08) — 电池传感器

*帧结构：payload = voltage(2) + current(2) + capacity(24bit) + remaining(1)*  
*总帧长 = 10 字节（含 type + crc），全大端序*

| 字段 | 类型 | 解析 | 单位 | 验证状态 |
|------|------|------|------|:-------:|
| voltage | uint16 | `(payload[0]<<8) \| payload[1]` | V × 10 (66=6.6V) | ✅ 6.6V ↔ 电源6.64V |
| current | uint16 | `(payload[2]<<8) \| payload[3]` | A × 10 (551=55.1A) | ✅ |
| capacity | uint32 (24bit) | `(payload[4]<<16) \| (payload[5]<<8) \| payload[6]` | mAh | ✅ |
| remaining | uint8 | `payload[7]` | % | ✅ |

### 4. FLIGHT_MODE (0x21) — 飞行模式

*帧结构：纯 ASCII 文本字符串，以 \0 结尾*

| 字段 | 说明 | 验证状态 |
|------|------|:-------:|
| flight_mode | 飞行模式名称 (如 "ACRO", "!ERR*") | ✅ 已验证 |

**注意：** `!ERR*` 表示接收机与飞控之间通信断开或未建立连接，属于正常行为。

### 5. GPS (0x02) — 位置信息

*帧结构：payload = lat(4) + lon(4) + speed(2) + heading(2) + alt(2) + sats(1)*  
*总帧长 = 17 字节（含 type + crc），全大端序*

| 字段 | 类型 | 解析 | 单位 | 验证状态 |
|------|------|------|------|:-------:|
| latitude | int32 | `(payload[0]<<24) \| (payload[1]<<16) \| (payload[2]<<8) \| payload[3]` | 度 × 1e7 | ✅ 已验证 |
| longitude | int32 | `(payload[4]<<24) \| (payload[5]<<16) \| (payload[6]<<8) \| payload[7]` | 度 × 1e7 | ✅ 已验证 |
| speed | uint16 | `(payload[8]<<8) \| payload[9]` | km/h / 10 | ✅ 已验证 |
| heading | uint16 | `(payload[10]<<8) \| payload[11]` | 度 / 100 | ✅ 已验证 |
| altitude | uint16 | `(payload[12]<<8) \| payload[13]` | m + 1000m | ✅ 已验证 |
| sats | uint8 | `payload[14]` | 颗 | ✅ 已验证 |

### 6. VARIO (0x07) — 垂直速度

*帧结构：payload = vSpeed(2)*  
*总帧长 = 4 字节，大端序*

| 字段 | 说明 | 验证状态 |
|------|------|:-------:|
| vSpeed | 垂直速度 cm/s | ⏳ 未验证 |

## 实现关键

### 字节序（坑点）

```c
// ❌ 错误：小端序读取（CRSF 全大端！）
(int16_t)(payload[0] | (payload[1] << 8));

// ✅ 正确：大端序读取
(int16_t)((payload[0] << 8) | payload[1]);
```

CRSF 协议规定：**所有多字节数值统一大端序（Big-Endian，MSB first）**。

### 帧头偏移（坑点）

标准遥测帧（ID < 0x28，如 0x02/0x08/0x1E/0x21 等）**没有 Destination 和 Origin 字节**，payload 从 `frame[3]`（即 `payload[0]`）直接开始。

只有扩展头帧（ID ≥ 0x28，如 PARAMETER_READ/WRITE）才有 origin 字节。

### 帧长度判断

每个帧的 `frame_len` 包含：type(1) + payload(N) + crc(1)，所以：
```c
// 姿态 6字节 payload → frame_len >= 8
if (frame_len >= 8) { ... }

// 电池 8字节 payload → frame_len >= 10
if (frame_len >= 10) { ... }
```

### 单位换算

| 字段 | Raw 值 | 实际值 | 换算 |
|------|--------|--------|------|
| voltage | 66 | 6.6V | raw / 10 |
| current | 551 | 55.1A | raw / 10 |
| attitude | 7714 | 44.2° | raw × 180 / 31415.9 |
| gps.lat | 223456780 | 22.345678° | raw / 1e7 |
| speed | 50 | 5.0 km/h | raw / 10 |
| heading | 4210 | 42.1° | raw / 100 |
| altitude | 1500 | 500m | raw - 1000 |

## 数据结构 `crsf_telemetry_t`

定义在 `include/rc_crsf.h`：

```c
typedef struct {
    struct {
        uint16_t voltage;       // V*10
        uint16_t current;       // A*10
        uint32_t capacity;      // mAh
        uint8_t remaining;      // %
    } battery;
    struct {
        int32_t latitude;       // 度*1e7
        int32_t longitude;      // 度*1e7
        uint16_t altitude;      // m + 1000m
        uint16_t speed;         // km/h / 10
        uint16_t heading;       // 度 / 100
    } gps;
    struct {
        int16_t pitch;          // rad*10000
        int16_t roll;           // rad*10000
        int16_t yaw;            // rad*10000
    } attitude;
    struct {
        int16_t vSpeed;         // cm/s
    } vario;
    uint8_t flight_mode[16];    // 字符串
    uint32_t last_update_ms;    // 最近一次回传时间戳
} crsf_telemetry_t;
```

通过 `crsf_get_state()->telemetry` 访问，例如：
```c
crsf_state_t *state = crsf_get_state();
state->telemetry.battery.voltage;   // 66 → 6.6V
state->telemetry.attitude.pitch;    // 7714 → 44.2°
```

## 主循环打印

```c
// main.c — 每 5s 输出

╔═══════════════════ CRSF 遥测 ═══════════════════╗
║ ⚡ 电池  6.6V / 55.1A  2579mAh  2%                      ║
║ 🛰 GPS    22.345678, 114.123456                              ║
║          65m  1cm/s  112°  19颗                  ║
║ 📐 姿态  Pitch +44.2°  Roll +0.5°  Yaw +42.1°            ║
║ 📊 气压  0m  +0.0m/s                               ║
║ 🏷 模式  ACRO                                  ║
╚══════════════════════════════════════════════════╝
```

## 待完成项

| 项目 | 状态 | 备注 |
|------|:----:|------|
| GPS 字段验证 | ⏳ | 需要接有 GPS 的飞控 |
| VARIO 验证 | ⏳ | 需要垂直移动 |
| Web 前端显示 | ⏳ | 需将 telemetry 通过 WebSocket 推送到前端 |
| 日志降噪 | ⏳ | 验证完成后移除 DEBUG 日志 (ATTITUDE/BATTERY/FLIGHT_MODE 的 ESP_LOGI) |

## 参考

- [AlfredoCRSF/CRSF](https://github.com/AlfredoCRSF/CRSF) — Arduino CRSF 库
- ExpressLRS / EdgeTX / OpenTX 源码 — CRSF 协议规范来源
- Betaflight 回传配置: 确保 `set serialrx_halfduplex = ON`, `set telemetry_inversion = ON`
