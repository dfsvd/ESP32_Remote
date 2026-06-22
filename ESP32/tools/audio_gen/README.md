# audio_gen — 手柄提示音语音生成工具

生成中文/英文 WAV 提示音，输出到 `ESP32/audio/`。生成的 WAV **不再编译进固件**，需部署到 TF 卡 `/audio/` 目录流式读取。

## 目录结构

```
tools/audio_gen/
├── README.md        ← 本文件
├── tts_gen.py       ← TTS 语音生成脚本
└── audio_list.csv   ← 语音列表 (filename, zh-CN, en-US)
```

## 依赖

```bash
# 使用 whisper-venv（已包含 edge-tts）
source /home/xinian/whisper-venv/bin/activate
# 或：pip install edge-tts
```

系统还需要 ffmpeg：
```bash
sudo pacman -S ffmpeg         # Arch
# 或 brew install ffmpeg      # macOS
# 或 apt install ffmpeg        # Ubuntu/Debian
```

## 用法

```bash
source /home/xinian/whisper-venv/bin/activate
cd ESP32/
python tools/audio_gen/tts_gen.py
```

脚本会：
1. 读取 `tools/audio_gen/audio_list.csv`
2. 用 edge-tts (微软 AI 语音) 合成中文语音
3. 经 ffmpeg 转为 16kHz/16bit/mono WAV
4. 输出到 `audio/` 目录

## audio_list.csv 格式

```csv
filename,zh-CN,en-US
hello.wav,开机,Welcome
armed.wav,已解锁,Armed
btmod.wav,蓝牙模式,Bluetooth mode
...
```

- **filename** — 输出的 WAV 文件名（对应 `rc_audio.h` 中的 `sound_id_t` 枚举）
- **zh-CN** — 中文播报文本
- **en-US** — 英文播报文本（当前 tts_gen.py 仅使用 zh-CN；加英文语音时使用 en-US）

## 与固件的关联

| 文件 | 作用 |
|------|------|
| `audio/*.wav` | 源码参考，需部署到 TF 卡 `/audio/` 目录（不再嵌入固件） |
| `components/RC/src/rc_audio.c` | `s_names[]` 文件名表 → 构造 `/audio/<name>.wav` 路径，SD 卡流式读取 |
| `components/RC/include/rc_audio.h` | `sound_id_t` 枚举定义，与 CSV 中的 `filename` 一一对应 |

## 部署到 TF 卡

生成 WAV 后，将 `ESP32/audio/` 目录全部复制到 TF 卡根目录：

```
TF卡根目录/
└── audio/
    ├── hello.wav
    ├── armed.wav
    ├── btcon.wav
    ├── btdcn.wav
    ... (28 files)
```

固件启动后自动挂载 SD 卡，播放时通过 `sdcard_fopen()` 流式读取，ping-pong 双缓冲写入 I2S DMA。

**添加新语音步骤：**

1. 在 `audio_list.csv` 添加一行
2. 运行 `python tools/audio_gen/tts_gen.py` 生成 WAV
3. 在 `rc_audio.h` 的 `sound_id_t` 添加枚举值
4. 在 `rc_audio.c` 的 `s_names[]` / `s_names_cn[]` 添加名称条目
5. 如需默认优先级，在 `s_priorities[]` 添加
6. 在 `main.c` 或其他地方调用 `audio_play()` 触发
7. 将新 WAV 复制到 TF 卡 `/audio/` 目录

## 参数调整

在 `tts_gen.py` 顶部可调参数：

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `VOICE` | `zh-CN-XiaoxiaoNeural` | edge-tts 发音人 |
| `RATE` | `+30%` | TTS 语速 |
| `VOL_TTS` | `+30%` | TTS 音量 |
| `FFMPEG_BOOST` | `6dB` | ffmpeg 增益 (补偿 TTS 电平偏低) |
| `CONCURRENCY` | 4 | 并行合成数 |
