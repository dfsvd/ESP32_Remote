---
name: project-status
description: 手柄项目当前状态总结 — 架构、硬件接线、模式选择、已知问题
metadata:
  type: project
---

## 项目概况

FPV 遥控器手柄，基于 ESP32-S3，16MB Flash + 8MB PSRAM。
两个子项目：**ESP32 固件**（ESP-IDF）和 **Web 前端**（Vue 3 + Vite）。

## 编译

### ESP32 固件

```bash
cd /home/xinian/Code/tusb_hid/ESP32/
source /home/xinian/.espressif/python_env/idf5.5_py3.14_env/bin/activate
export IDF_PATH=/home/xinian/.espressif/v5.5.4/esp-idf
. /home/xinian/.espressif/v5.5.4/esp-idf/export.sh
idf.py build
```

或一行完成：
```bash
cd /home/xinian/Code/tusb_hid/ESP32/ && source /home/xinian/.espressif/python_env/idf5.5_py3.14_env/bin/activate 2>/dev/null && export IDF_PATH=/home/xinian/.espressif/v5.5.4/esp-idf && . /home/xinian/.espressif/v5.5.4/esp-idf/export.sh > /dev/null 2>&1 && idf.py build
```

### 烧录 + 监视

```bash
idf.py flash monitor
```

### Web 前端（Vue 3 + Vite）

```bash
cd /home/xinian/Code/tusb_hid/web
npm install
npm run dev              # 开发服务器 http://localhost:5174
npm run build            # 构建到 dist/
npm run build:esp32      # 构建 + 同步到 ESP32/components/RC/src/index.html
```

## 硬件接线

### 摇杆 (ADC)
| 通道 | ADC 通道 | GPIO |
|------|---------|------|
| Roll | ADC1_CH5 | GPIO6 |
| Pitch | ADC1_CH6 | GPIO7 |
| Throttle | ADC1_CH4 | GPIO5 |
| Yaw | ADC1_CH3 | GPIO4 |

### 开关 (GPIO)
按遥控器物理标识命名，都在 ADC_TASK 中轮询（~156Hz）。

| 开关 | GPIO | 类型 | 输出通道 |
|------|------|------|---------|
| SA | 36 | 自复位按键 | CH5 |
| SB | 37 | 2段拨码 | CH6 |
| SC | **39（单线）** | 3段拨码 | CH7 |
| SD | 38 | 自复位按键 | CH8 |

**SC 三段开关单线方案：**
- 公共端接 GPIO39，上档接 3.3V，下档接 GND，中档悬空
- 读取时：设内部上拉读一次，再设内部下拉读一次
  - 都=1 → VCC（上档）→ 2000
  - 都=0 → GND（下档）→ 1000
  - 一个1一个0 → 悬空（中档）→ 1500
- 延时 15µs 保证 RC 充放电完成
- 连续 3 次一致（~19ms）才确认，覆盖机械抖动
- GPIO40 已释放

### 音频 (MAX98357A I2S)
| 信号 | GPIO |
|------|------|
| BCLK | 11 |
| LRC/WS | 12 |
| DIN | 13 |

### 其他
| 功能 | GPIO |
|------|------|
| WS2812 LED | 48 |
| APP_BUTTON | 0 |
| CRSF TX | 17 |
| CRSF RX | 16 |
| 串口 | 43(TX) 44(RX) |

相关记忆：[[hardware-specs]]

## 开机模式选择

统一 `boot_mode_t` 枚举，检测与初始化分离。

SD 按下 → **自动对频**（纯射频 + 播射频模式）
SA 长按 3秒 → **摇杆选择**：
  - 上推(pitch < 1300) → **USB 模式**（播 USB 模式，等播完再 init USB）
  - 下推(pitch > 1700) → **BLE 模式**（播蓝牙模式，等播完再 init BLE）
  - 右推(roll > 1700) → **纯射频模式**（播射频模式）
  - **SD 按下** → **WiFi 模式**（播 Wi-Fi 模式后等播完再 init WiFi + CRSF）
  - 超时(5s) → 回退上次模式（当前写死为 BOOT_MODE_RF，未来从 NVS 读）
SA 短按(<3s) → 回退上次模式
无按键 → 回退上次模式

兜底逻辑：用户未主动选择 → `get_default_mode()` 取上次保存的模式。
当前写死 `BOOT_MODE_RF`，未来改 `nvs_read_boot_mode()` 即可。

**CRSF 条件初始化：** 只在纯射频 / 自动对频 / WiFi 模式下启动。
BLE 和 USB 模式下不初始化 CRSF，省电。

## 音频系统

- `components/RC/include/rc_audio.h` + `src/rc_audio.c`
- 后台 FreeRTOS 任务 + 队列（深度 4）
- 4 级优先级：CRITICAL > HIGH > NORMAL > LOW
- `audio_play_wait(id, timeout)` — 用二值信号量阻塞等待播完
- `VOLUME_GAIN_PCT = 50`（默认音量 50%，避免棕出）
- I2S 只有播放时才 enable，播完 disable 防 DMA 空转噪声
- 32 个中文语音 WAV（~1.4MB）通过 `EMBED_FILES` 嵌入固件
- 语音列表：`tools/audio_gen/audio_list.csv`
- 生成命令：`source /home/xinian/whisper-venv/bin/activate && cd ESP32/ && python tools/audio_gen/tts_gen.py`

当前已挂载的语音触发点：
- 开机 → SOUND_HELLO
- BLE 连接 → SOUND_BTCON
- BLE 断开 → SOUND_BTDCN
- 模式选择 → 对应模式语音（蓝牙/USB/WiFi/射频）

## 分区表

```csv
nvs      0x9000   0x6000     NVS
otadata  0xF000   0x2000     OTA 元数据
phy_init 0x11000  0x1000     射频校准
factory  0x20000  0x800000   固件 (8MB)
```

`sdkconfig.defaults` 设定了 `CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y`。

## 已知问题

- **棕出问题（Brownout）：** MAX98357A 功放 + CRSF/WiFi 同时满载时瞬时电流 > 3.3V 稳压器能力。已采取：音量降到 50%、模式切换先播语音等播完再 init 硬件。
- GPIO46 未使用（可能是 `compile_commands.json` 等工具生成的文件）。
- `print_joystick_snapshot` 未使用（保留的调试函数）。

**Why:** 总结当前项目架构与决策，方便后续恢复会话时快速上手。

**How to apply:** 每次恢复会话时 MEMORY.md 会加载，此文件提供完整上下文。相关记忆用 [[name]] 交叉引用。
