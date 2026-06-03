---
name: hardware-specs
description: 手柄项目的硬件存储器规格（ESP32 模组）
metadata:
  type: project
---

**硬件存储规格（ESP32 模组）：**
- 内置 SRAM：512 KB
- 内置 ROM：384 KB
- 外置 Flash：**16 MB**（大容量）
- 外置 PSRAM：**8 MB**

**关键影响：**
- 16MB Flash 空间充裕，`partitions.csv` 中 factory 分区设为 8MB（`0x800000`），足够容纳固件 + 中文语音 + 英文语音 + 未来扩展
- 当前分区：`factory 0x20000 → 0x820000`（8MB），剩余 ~8MB 未分配
- PSRAM 8MB 为 Web 服务器等提供大量堆内存，音频 WAV 通过 EMBED_FILES 直接嵌入 Flash 映射地址空间，不占用 RAM
- 若未来 WAV 文件继续增多，8MB factory 分区仍有余量；超量后可改用 SPIFFS/storage 分区存储

**分区布局：**
```
nvs      0x9000    0x6000     NVS 配置
otadata  0xF000    0x2000     OTA 元数据
phy_init 0x11000   0x1000     射频校准
factory  0x20000   0x800000   工厂固件 (8MB) ← 固件 + 中/英文 WAV
```
