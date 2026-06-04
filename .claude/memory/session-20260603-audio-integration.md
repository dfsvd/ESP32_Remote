---
name: session-20260603-audio-integration
description: 本次会话工作内容 — 语音播报系统集成、棕出修复、纯射频模式独立
metadata:
  type: project
---

## 已完成

### 1. 语音播报系统集成 (commit: b558bfd)
- 从 `hello_world` 项目将 `audio_player.h/c` 移植到 `components/RC/include/rc_audio.h` + `components/RC/src/rc_audio.c`
- 32 个中文语音 WAV (~1.4MB) 放入 `ESP32/audio/`，通过 EMBED_FILES 嵌入固件
- I2S 引脚: BCLK=11, LRC=12, DIN=13 (MAX98357A)
- 分区表: factory 扩到 8MB (0x800000)
- sdkconfig.defaults: CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
- RC 组件 CMakeLists.txt 添加 `esp_driver_i2s` 依赖 + EMBED_FILES

### 2. 棕出 (Brownout) 修复 (commit: 2ee5981)
- 新增 `audio_play_wait(id, timeout)` API — 用二值信号量实现真正的阻塞等待播完
- 所有模式切换处先播语音播完再初始化硬件
- 默认音量 `VOLUME_GAIN_PCT: 100 → 50`

### 3. 纯射频模式独立 + CRSF 条件初始化 (commit: 7de611b)
- `detect_boot_mode()` 新增 `rf_mode` 参数
- SA长按后: 右摇杆上=BLE, 下=USB, **右=纯射频**
- CRSF 仅在纯射频/WiFi/对频模式下初始化，BLE/USB 不启 CRSF
- 主循环 CRSF 同步放在 `crsf_needed` 条件内
- 新增 `SOUND_RFMOD` (射频模式) 语音 + `audio/rfmod.wav`

### 4. 修复: 默认射频无语音 (commit: db1a28d)
- 所有射频入口（默认无按键、SD对频、超时回退）均添加 SOUND_RFMOD 语音

### 5. 工具整理
- `tools/audio_gen/` — tts_gen.py + audio_list.csv + README.md
- 生成命令: `source /home/xinian/whisper-venv/bin/activate && python tools/audio_gen/tts_gen.py`
- 输出指向 `ESP32/audio/`

### 6. 仓库调整
- main 分支已替换为 dev 内容并强制推送
- 旧 dev 分支已删除

## 待办 / 后续
- [ ] 添加英文语音（en-US 列）
- [ ] 后续其他事件触发语音（CRSF链路、低电量等）
- [ ] 如有需要可降低音量进一步避免棕出

**Why:** 语音系统已完整可用，下次继续时可在此基础上加更多触发事件或英文语音。

**How to apply:** 不需要特殊操作 — 代码已在 main 分支上提交并推送。
