# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build & Flash Commands

### ESP32 Firmware (ESP-IDF, target: esp32s3)

**Environment setup (required once per shell):**
```powershell
$env:IDF_PATH = "D:\Program\Espressif\frameworks\esp-idf-v5.5.4"
$env:PYTHON   = "D:\Program\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe"
```

**Configure (cmake init, from ESP32/ directory):**
```powershell
cmake -G Ninja -DPYTHON_DEPS_CHECKED=1 -DESP_PLATFORM=1 -B build -S .
```

**Build:**
```powershell
cmake --build build -- -j8
```

**Full flash + monitor (replace COM_PORT, env must be set):**
```powershell
idf.py -p COM_PORT flash monitor
```

### Web Frontend (Vue 3 + Vite)
```bash
cd web
npm install               # Install deps
npm run dev               # Dev server on http://localhost:5174
npm run build             # Build to dist/
npm run build:esp32       # Build + sync to ESP32/components/RC/src/index.html
```

**Note:** `build:esp32` copies the single-file output into the ESP32 component so it gets embedded in firmware at compile time (not SPIFFS).

### Audio WAV Generation
```bash
cd ESP32/tools/audio_gen
python tts_gen.py         # Uses edge-tts to batch-generate WAV files from audio_list.csv
```

### MSP Capture Utility
```bash
python tools/capture_msp.py  # Capture Betaflight MSP communication for debugging
```

## Project Overview

Two independent sub-projects: **ESP32 firmware** (`ESP32/`) and **Web frontend** (`web/`). The web UI is compiled into a single `index.html` via `vite-plugin-singlefile` and embedded directly into the ESP32 firmware binary at compile time. No SPIFFS upload needed.

The firmware implements an **FPV RC controller** with multiple output modes (USB HID, BLE HID, CRSF RF), a **bridge mode** (BLE NUS ↔ USB CDC / CRSF MSP) for flight controller passthrough, and a WiFi AP for configuration. Target hardware: ESP32-S3.

## Boot Modes

Mode selection at startup: hold SA button for 3s, then use right stick to choose (5s timeout):

| Direction | Mode | Description |
|-----------|------|-------------|
| Stick Up | USB Xbox | Xbox 360 controller via TinyUSB HID |
| Stick Down | BLE FPV | BLE HID gamepad via NimBLE ("FPV RC BLE V2") |
| Stick Right | Pure RF | CRSF only, no USB/BLE/WiFi |
| Stick Left | Passthrough | BLE NUS ↔ USB CDC bridge to flight controller |
| SD Button | WiFi AP | WiFi AP + WebSocket config page |
| Timeout | RF (default) | Falls back to last saved mode or pure RF |

Auto-bind: when CRSF link is not connected, the firmware periodically sends bind commands via CRSF menu system until the receiver links up.

## ESP32 Firmware Architecture

All source is in `ESP32/components/RC/`. Modules communicate through a shared `fpv_joystick_report_t` struct (16 channels, 1000-2000 range).

### Core Modules

- **rc_read** (`include/rc_read.h`, `src/rc_read.c`) — ADC continuous reading at 20kHz. Reads 4 analog sticks, 4 aux pots, 4 switches (SA=button, SB=2pos, SC=3pos, SD=button). NVS calibration storage with profiles, EPA (end-point adjustment), REV (reverse), channel mapping, stick mode config.

- **rc_usb** — TinyUSB HID device. Two modes via `sim_mode_t`: standard HID (8 axes + 24 buttons) and Xbox 360 (`xinput_report_t`, 20-byte packet).

- **rc_ble** — BLE HID over GATT via Apache NimBLE. Two BLE modes: `BLE_MODE_HID` (gamepad) and `BLE_MODE_NUS` (Nordic UART Service for bridge data). Input via single-element queue to prevent concurrent reads.

- **rc_crsf** — CRSF protocol for external radio modules (ExpressLRS, etc). Supports full-duplex (separate TX/RX pins) and half-duplex (single wire). Implements menu/param system (folder, text, info, command, option item types), device info callback, telemetry parsing (battery, GPS, attitude, vario, flight mode).

- **rc_wf** — WiFi AP + HTTP server + WebSocket server. Serves the embedded `index.html`, streams channel data, receives calibration/CRSF/LED commands.

- **rc_bridge** — BLE NUS ↔ (CRSF MSP | USB CDC) bidirectional bridge. Used in passthrough mode for flight controller configuration via MSP protocol.

- **rc_usb_host** — USB Host CDC ACM for connecting to flight controllers (Betaflight/INAV). Handles hot-plug, bus bounce reset, ring buffer read/write.

- **rc_audio** — I2S + MAX98357A audio player. 28 sound effects (WAV), 4 priority levels (CRITICAL > HIGH > NORMAL > LOW), background task-driven non-blocking playback, blocking `audio_play_wait()` for boot-time announcements.

- **rc_led** — WS2812 addressable LED controller. 7 mode presets (OFF, CRSF_RF→red, BLE→blue, USB_HID→cyan, USB_XBOX→lime, WIFI→green, BIND→yellow fast blink). 4 effects (solid, blink, breath, rainbow). Runtime reconfigurable via Web UI.

### Main Loop (`main/main.c`)

Startup sequence: NVS init → load settings → GPIO config → audio init → start ADC task → LED init → detect boot mode → init selected modules → while(1): BLE update, LED poll, CRSF sync + auto-bind FSM + RSSI alerts + switch edge detection, USB HID send.

The main loop handles:
- **Link state tracking** — voice alerts on link up/down (`SOUND_TELEMOK`/`SOUND_TELEMKO`)
- **Switch edge detection** — SB (arm/disarm) triggers `SOUND_ARMED`/`SOUND_LOCKED`
- **RSSI alerts** — periodic voice warnings below threshold
- **SD bind button** — press to initiate CRSF bind when not connected
- **Menu loading** — logs CRSF menu snapshot once fully loaded

### Data Flow

```
ADC (20kHz) → fpv_joystick_report_t {roll, pitch, throttle, yaw,
                                      aux1-4, sw1-8 (1000-2000)}
              │
              ├─ USB HID → tud_mounted() ? app_send_fpv_data(&joy)
              ├─ BLE HID → ble_update_input(&joy) [queue-based]
              ├─ CRSF RF → sync_joy_to_crsf(&joy) [16 channels]
              ├─ WiFi WS → rc_wf broadcasts CSV to web clients
              └─ Bridge  → BLE NUS ↔ USB CDC or CRSF MSP
```

## Web Frontend Architecture

Vue 3 + Vite 8 + Tailwind CSS 3, Composition API + `<script setup>` throughout. Hash-based routing (required for single-file embedded deployment).

### Structure (`web/src/`)
```
router/index.js          — Hash router, auto-redirects by browser language (/zh or /en)
views/Main_zh.vue       — Full-page Chinese layout (Dashboard, Config, LED, Telemetry tabs)
views/Main_en.vue       — Full-page English layout (same structure as Main_zh)
composables/useRCState.js — Central state management: WebSocket, channels, CRSF, LED, profiles, calibration
components/             — Chinese UI components
components_en/          — English UI components (imported into Main_en.vue)
```

### Key Components

- **WebSocketConnection** — Headless WebSocket manager (auto-connect, exponential backoff reconnect, send/receive). Single component per language variant.
- **Joystick** — Circular virtual joystick, 150×150px, constrained-to-circle rendering.
- **AuxChannelSlider** — Expandable channel card: calibrated percentage slider, raw value slider, Min/Mid/Max calibration inputs with "Set" buttons.
- **CalibrationModal** — Step-by-step joystick calibration wizard.
- **CrsfConfiguratorPanel** — CRSF menu browser: folders, options, commands, info items.
- **LedConfiguratorPanel** — LED mode/effect/color/brightness configurator with live preview.
- **TelemetryPanel** — Real-time flight telemetry display (battery, GPS, attitude, vario).
- **ChannelBar** — Compact channel value indicator bars.
- **ConfigChannelMapping** / **ConfigChannelProps** — Switch-to-channel mapping table, EPA + REV configuration.
- **ConfigProfiles** — Profile save/load/export/import (up to 8 profiles stored in NVS).
- **StickDiagram** — SVG stick mode diagram.
- **ConfigStickMode** — Mode 1/2 selection with diagram.

### WebSocket Protocol

**ESP32 → Web:** Comma-separated 16 channel values (CSV), or prefixed messages:
- `1500,1500,1000,...` — channel data
- `C:1,1000,1500,2000;2,...` — calibration data
- `EPA:1,100,100;2,...` — end-point adjustment
- `REV:0x0000` — reverse mask
- `MAP:0,1,2,...` — channel mapping
- `CRSF_SNAPSHOT:{...}` — CRSF menu state JSON
- `TELEMETRY:{...}` — flight telemetry JSON

**Web → ESP32:** Commands:
- `C:1,1000,1500,2000;2,...` — save calibration
- `EPA:1,100,100;2,...` — set EPA
- `REV:0x0000` — set reverse mask
- `MAP:0,1,2,...` — set channel mapping
- `CRSF_GET_MENU` / `CRSF_SET:id,value` — CRSF menu navigation
- `LED:id,r,g,b,effect,brightness,interval` — LED configuration

## Key Configuration Files

- `ESP32/sdkconfig.defaults` — NimBLE enabled (not Bluedroid), TinyUSB HID, custom partition table, 16MB flash, LWIP 20 sockets
- `ESP32/partitions.csv` — nvs (0x6000), otadata (0x2000), phy_init (0x1000), factory (8MB)
- `ESP32/dependencies.lock` — esp_tinyusb 2.1.1, led_strip 3.0.3, tinyusb 0.19.0~3
- `ESP32/components/RC/include/rc_crsf.h` — CRSF link mode (half/full duplex) and UART pins
- `ESP32/components/RC/include/rc_read.h` — ADC channels, switch GPIO pins, calibration structs, config blob (channel mapping, EPA, REV, profiles)

## Reference Docs

- `docs/crsf-telemetry.md` — CRSF telemetry implementation details
- `docs/ADC调试模块.md` — ADC debug module notes
- `docs/开关机方案对比.md` — Power on/off scheme comparison
- `docs/启动模式设计.md` — Boot mode design document
- `docs/透穿调参模式_可行性研究报告.md` — Passthrough tuning mode feasibility study
- `boot_mode_plan.md` — Boot mode refactoring plan
