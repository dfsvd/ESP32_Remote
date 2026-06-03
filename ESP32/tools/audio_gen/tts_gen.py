#!/home/xinian/whisper-venv/bin/python3
"""批量按 CSV 生成 WAV 到 ESP32/audio/.

使用方法:
  source /home/xinian/whisper-venv/bin/activate
  cd ESP32/
  python tools/audio_gen/tts_gen.py

依赖:
  使用 whisper-venv 中的 edge-tts（已安装）
  需要系统中安装 ffmpeg

CSV 格式 (tools/gen_audio/audio_list.csv):
  filename,zh-CN,en-US
  armed.wav,已解锁,Armed
  ...
"""
import asyncio
import csv
import subprocess
import sys
from pathlib import Path

# ---- 路径配置 ----
# 脚本在 tools/audio_gen/ 下，项目根目录向上两层
PROJECT_ROOT = Path(__file__).resolve().parents[2]
SRC_CSV = PROJECT_ROOT / "tools" / "audio_gen" / "audio_list.csv"
OUT_DIR = PROJECT_ROOT / "audio"

VOICE = "zh-CN-XiaoxiaoNeural"
RATE = "+30%"
VOL_TTS = "+30%"
FFMPEG_BOOST = "6dB"
CONCURRENCY = 4


async def synth_one(sem, text: str, wav_path: Path):
    async with sem:
        mp3 = wav_path.with_suffix(".mp3")
        try:
            comm = edge_tts.Communicate(text, voice=VOICE, rate=RATE, volume=VOL_TTS)
            await comm.save(str(mp3))
            subprocess.run(
                ["ffmpeg", "-y", "-loglevel", "error",
                 "-i", str(mp3),
                 "-af", f"volume={FFMPEG_BOOST}",
                 "-ar", "16000", "-ac", "1", "-c:a", "pcm_s16le",
                 str(wav_path)],
                check=True,
            )
            return (wav_path.name, "ok", wav_path.stat().st_size)
        except Exception as e:
            return (wav_path.name, f"ERR: {e}", 0)
        finally:
            if mp3.exists():
                mp3.unlink()


async def main():
    if not SRC_CSV.exists():
        print(f"缺少 CSV 文件: {SRC_CSV}")
        print("请确保 tools/gen_audio/audio_list.csv 存在")
        sys.exit(1)

    OUT_DIR.mkdir(exist_ok=True)

    # 读取 CSV
    rows = []
    with SRC_CSV.open(encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for r in reader:
            rows.append((r["filename"], r["zh-CN"]))
    print(f"待生成: {len(rows)} 条 → {OUT_DIR}")

    sem = asyncio.Semaphore(CONCURRENCY)
    tasks = [synth_one(sem, txt, OUT_DIR / fn) for fn, txt in rows]
    results = []
    for i, coro in enumerate(asyncio.as_completed(tasks), 1):
        r = await coro
        results.append(r)
        print(f"[{i:2}/{len(rows)}] {r[0]:16s} {r[1]:8s} {r[2]:>7} B")

    ok = sum(1 for r in results if r[1] == "ok")
    print(f"\n完成: {ok}/{len(results)}  失败: {len(results) - ok}")


if __name__ == "__main__":
    asyncio.run(main())
