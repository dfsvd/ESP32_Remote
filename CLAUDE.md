# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build & Flash Commands

### ESP32 Firmware (ESP-IDF)
```bash
# Build, flash, monitor (from ESP32/ directory)
idf.py flash monitor
```

### Web Frontend (Vue 3 + Vite)
```bash
cd web
npm install          # Install deps
npm run dev          # Dev server on http://localhost:5174
npm run build        # Build to dist/
npm run build:esp32  # Build + sync to ESP32/components/RC/src/index.html
```

## Project Architecture

Two independent sub-projects: **ESP32 firmware** (`ESP32/`) and **Web frontend** (`web/`).

### ESP32 Firmware — FPV RC Controller

Built with ESP-IDF, targets ESP32. Boot mode is selected by GPIO pins at startup:

| CH1 | 3pos Up | 3pos Down | Mode |
|-----|---------|-----------|------|
| Low | Low | - | BLE HID (NimBLE) |
| Low | - | Low | WiFi AP + WebSocket |
| Low | Mid (neither low) | Mid | USB HID / Xbox |
| High | - | - | Pure RF (CRSF only) |

CH3 low = enable auto-bind mode on CRSF link.

Key modules in `ESP32/components/RC/`:

- **rc_read.c/h** — ADC continuous reading of joysticks/pots/switches (20kHz), NVS calibration storage. Defines `fpv_joystick_report_t` (16 channels, 1000-2000 range) and GPIO switch pin macros.
- **rc_usb.c/h** — TinyUSB HID device. Two modes: standard HID (4 axes + buttons) and Xbox 360 (xinput_report_t 20-byte packet). `sim_mode_t` enum controls mode.
- **rc_ble.c/h** — BLE HID over GATT via Apache NimBLE. Device name "FPV RC BLE V2". Input via single-element queue to avoid concurrent reads.
- **rc_crsf.c/h** — CRSF protocol for external radio module (ExpressLRS, etc). Supports full-duplex (TX/RX separate) and half-duplex (single wire). Menu/param system, device info callback.
- **rc_wf.c/h** — WiFi AP + HTTP server + WebSocket server. Serves the embedded web UI, streams channel data via WebSocket, receives calibration and CRSF commands.

Main loop (`main.c`): NVS init → GPIO config → CRSF init → ADC task start → boot mode detect → while(1): BLE update, CRSF sync, auto-bind FSM, USB HID send, menu load tracking.

### Web Frontend — Configuration UI

Vue 3 + Vite + Tailwind CSS. Built with `vite-plugin-singlefile` → single `index.html` embedded in ESP32 SPIFFS.

- **Router:** `web/src/router/index.js` — hash-based routing, auto-redirects by browser language (`/zh` or `/en`)
- **Views:** `Main_zh.vue` / `Main_en.vue` — full-page layouts with channel display, joystick visualization, calibration modal, CRSF config panel
- **Components:** AuxChannelSlider, Joystick, CalibrationModal, CrsfConfiguratorPanel, WebSocketConnection
- **Communication:** WebSocket to ESP32 — channel data stream (CSV: `1500,1500,...`) and calibration commands (`C:1,1000,1500,2000;2,...`)

### Data Flow

```
ADC (20kHz) → fpv_joystick_report_t → shared by all modules
                                       ├─ USB HID (tud_mounted)
                                       ├─ BLE HID (queue-based)
                                       ├─ CRSF (sync_joy_to_crsf)
                                       └─ WiFi WebSocket (rc_wf broadcasts)
```

## Key Configuration Files

- `ESP32/sdkconfig.defaults` — NimBLE enabled, TinyUSB HID, custom partition table
- `ESP32/components/RC/include/rc_crsf.h` — CRSF link mode (half/full duplex) and UART pins
- `ESP32/components/RC/include/rc_read.h` — ADC channels, switch GPIO pins, calibration structs
