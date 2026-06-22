/* SPDX-License-Identifier: Unlicense */
/*
 * ESP32 手柄提示音播放器 — 实现
 *
 * 依赖:
 *   - MAX98357A I2S 功放 (BCLK=19, LRC=20, DIN=37)
 *   - I2S 标准 Philips 立体声模式 (MAX98357A 要求)
 *   - 16bit/16000Hz/mono PCM WAV 输入 → 立体声输出
 *   - TF 卡 /sd/audio/ 目录 WAV 文件流式读取
 *
 * 播放模型:
 *   - 后台 FreeRTOS 任务 + 队列 (深度 4)
 *   - 非阻塞: audio_play() 立即返回
 *   - 打断: 高优先级请求可中断当前播放
 *   - 丢弃: 队列满时新请求被丢弃
 *   - I2S DMA ping-pong 双缓冲: 预加载两帧后使能, 消除帧间隙
 */

#include "rc_audio.h"
#include "rc_sdcard.h"
#include "driver/i2s_std.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "audio_player";

/* ================================================================
 * 硬件默认值
 * ================================================================ */
#define DEFAULT_BCLK GPIO_NUM_11
#define DEFAULT_LRC GPIO_NUM_12
#define DEFAULT_DOUT GPIO_NUM_13
#define DEFAULT_SAMPLE_RATE 16000

/* ================================================================
 * 缓冲区
 * ================================================================ */
#define MONO_BUF_SZ 1024                // 单声道输入块大小
#define STEREO_BUF_SZ (MONO_BUF_SZ * 2) // 单帧立体声输出块大小
#define PLAY_QUEUE_LEN 4                // 播放队列深度
#define AUDIO_TASK_STACK 4096
#define AUDIO_TASK_PRIO 5

/* 音量增益 */
#define VOLUME_GAIN_PCT 10

/* ================================================================
 * WAV 文件头 (44 bytes 标准格式)
 * 注意: 部分 WAV 在 fmt chunk 后有 LIST/INFO 等扩展块,
 *       需要搜索 "data" 标记而非假定固定偏移
 * ================================================================ */
typedef struct {
    char riff[4]; // "RIFF"
    uint32_t file_size;
    char wave[4]; // "WAVE"
    char fmt[4];  // "fmt "
    uint32_t fmt_size;
    uint16_t audio_format;
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
} __attribute__((packed)) wav_fmt_t;

/*
 * 在 WAV 文件中搜索 "data" chunk
 * 返回 data chunk 内的 payload 偏移及大小
 * 失败返回 0
 */
static size_t wav_find_data(const uint8_t *start, const uint8_t *end,
                            const uint8_t **data_ptr, uint32_t *data_size) {
    const uint8_t *p = start + 12; // 跳过 RIFF+WAVE
    while (p + 8 < end) {
        if (p[0] == 'd' && p[1] == 'a' && p[2] == 't' && p[3] == 'a') {
            *data_ptr = p + 8;
            *data_size = (uint32_t)p[4] | ((uint32_t)p[5] << 8) |
                         ((uint32_t)p[6] << 16) | ((uint32_t)p[7] << 24);
            return (size_t)(p - start); // "data" 标识位置偏移
        }
        /* 跳过当前 chunk: 4字节ID + 4字节size + payload */
        uint32_t chunk_size = (uint32_t)p[4] | ((uint32_t)p[5] << 8) |
                              ((uint32_t)p[6] << 16) | ((uint32_t)p[7] << 24);
        p += 8 + chunk_size;
    }
    return 0; // 未找到
}

/* ================================================================
 * 每个提示音的默认优先级
 * ================================================================ */
static const audio_priority_t s_priorities[SOUND_COUNT] = {
    [SOUND_THRALERT] = AUDIO_PRIO_CRITICAL,
    [SOUND_SWALERT] = AUDIO_PRIO_CRITICAL,

    [SOUND_RSSI_RED] = AUDIO_PRIO_HIGH,
    [SOUND_RSSI_ORG] = AUDIO_PRIO_HIGH,
    [SOUND_LOWBATT] = AUDIO_PRIO_HIGH,
    [SOUND_LOWBAT] = AUDIO_PRIO_HIGH,
    [SOUND_TELEMKO] = AUDIO_PRIO_HIGH,
    [SOUND_SENSORKO] = AUDIO_PRIO_HIGH,

    [SOUND_INACTIV] = AUDIO_PRIO_LOW,
    /* 其余均为 AUDIO_PRIO_NORMAL (默认 0) */
};

/* ================================================================
 * 名称查找表
 * ================================================================ */
static const char *s_names[] = {
    [SOUND_HELLO] = "hello",       [SOUND_ARMED] = "armed",
    [SOUND_MODESW] = "modesw",     [SOUND_FPVMOD] = "fpvmod",
    [SOUND_BTMOD] = "btmod",       [SOUND_WIFIMD] = "wifimd",
    [SOUND_USBMOD] = "usbmod",     [SOUND_XBOXMOD] = "xboxmod",
    [SOUND_WIFICON] = "wificon",   [SOUND_WIFIDCN] = "wifidcn",
    [SOUND_BTCON] = "btcon",       [SOUND_BTDCN] = "btdcn",
    [SOUND_LOWBATT] = "lowbatt",   [SOUND_LOWBAT] = "lowbat",
    [SOUND_RSSI_ORG] = "rssi_org", [SOUND_RSSI_RED] = "rssi_red",
    [SOUND_TELEMOK] = "telemok",   [SOUND_TELEMKO] = "telemko",
    [SOUND_SENSORKO] = "sensorko", [SOUND_MODELPWR] = "modelpwr",
    [SOUND_THRALERT] = "thralert", [SOUND_SWALERT] = "swalert",
    [SOUND_INACTIV] = "inactiv",   [SOUND_RFMOD] = "rfmod",
    [SOUND_BINDING] = "binding",   [SOUND_BINDFAIL] = "bindfail",
    [SOUND_LOCKED] = "lock",       [SOUND_MSCMOD] = "mscmod",
    [SOUND_PASMOD] = "pasmod",
};

static const char *s_names_cn[] = {
    [SOUND_HELLO] = "开机",
    [SOUND_ARMED] = "已解锁",
    [SOUND_MODESW] = "模式切换",
    [SOUND_FPVMOD] = "FPV模式",
    [SOUND_BTMOD] = "蓝牙模式",
    [SOUND_WIFIMD] = "WiFi模式",
    [SOUND_USBMOD] = "USB模式",
    [SOUND_XBOXMOD] = "Xbox模式",
    [SOUND_WIFICON] = "WiFi连接",
    [SOUND_WIFIDCN] = "WiFi断开",
    [SOUND_BTCON] = "蓝牙连接",
    [SOUND_BTDCN] = "蓝牙断开",
    [SOUND_LOWBATT] = "遥控器电压低",
    [SOUND_LOWBAT] = "电池电压低",
    [SOUND_RSSI_ORG] = "射频信号弱",
    [SOUND_RSSI_RED] = "射频信号危险",
    [SOUND_TELEMOK] = "回传恢复",
    [SOUND_TELEMKO] = "回传丢失",
    [SOUND_SENSORKO] = "传感器丢失",
    [SOUND_MODELPWR] = "接收机未关闭",
    [SOUND_THRALERT] = "油门不在最低",
    [SOUND_SWALERT] = "开关不在初始位置",
    [SOUND_INACTIV] = "长时间无操作",
    [SOUND_RFMOD] = "射频模式",
    [SOUND_BINDING] = "正在连接接收机",
    [SOUND_BINDFAIL] = "连接失败",
    [SOUND_LOCKED] = "已锁定",
    [SOUND_MSCMOD] = "U盘模式",
    [SOUND_PASMOD] = "透穿调参模式",
};

/* ================================================================
 * 内部状态
 * ================================================================ */
typedef struct {
    sound_id_t id;
    audio_priority_t priority;
} play_request_t;

static i2s_chan_handle_t s_i2s_handle = NULL;
static QueueHandle_t s_queue = NULL;
static TaskHandle_t s_task = NULL;
static volatile bool s_playing = false;
static volatile bool s_stop_req = false;
static SemaphoreHandle_t s_play_done = NULL; /* 同步信号量: audio_play_wait */

/*
 * 双缓冲 (ping-pong) 缓冲区 — BSS 段, 不占栈
 *
 *   s_audio_buf[0]: ping 帧 (2048 字节 = 1024 samples × 16bit × 2ch)
 *   s_audio_buf[1]: pong 帧 (同上)
 *   启动前预加载两帧到 DMA → I2S enable → 交替填充 buf_idx ^= 1
 */
static uint8_t s_audio_buf[2][STEREO_BUF_SZ]; /* 2 × 2048 B */

/* 单声道读取缓冲区: 每次从 SD 卡读 1024 字节 PCM */
static uint8_t s_mono_buf[MONO_BUF_SZ];

/* ================================================================
 * mono → stereo 转换 + 增益 + 硬限幅
 * 输入:  [S0l,S0h, S1l,S1h, ...]  (16bit mono PCM)
 * 输出:  [S0l,S0h, S0l,S0h, S1l,S1h, S1l,S1h, ...] (16bit stereo)
 * gain:  100=原始, 150=+3.5dB, 200=+6dB
 * out_buf: 输出目标缓冲区 (必须 ≥ mono_len × 2 字节)
 * ================================================================ */
static size_t mono_to_stereo(const uint8_t *mono, size_t mono_len,
                             uint8_t gain_pct, uint8_t *out_buf) {
    size_t samples = mono_len / 2;
    size_t out_len = samples * 4;

    if (out_len > STEREO_BUF_SZ) {
        samples = STEREO_BUF_SZ / 4;
        out_len = samples * 4;
    }

    const int16_t *in = (const int16_t *)mono;
    int16_t *out = (int16_t *)out_buf;

    for (size_t i = 0; i < samples; i++) {
        int32_t s = (int32_t)in[i] * gain_pct / 100;
        if (s > 32767)
            s = 32767;
        if (s < -32768)
            s = -32768;

        out[i * 2] = (int16_t)s;     // 左
        out[i * 2 + 1] = (int16_t)s; // 右 = 左
    }

    return out_len;
}

/* ================================================================
 * 播放一个提示音 (任务内调用)
 *
 * 数据来源: TF 卡 /sd/audio/<name>.wav
 * 播放模型: ping-pong 双缓冲
 *   1. sdcard_fopen 打开 WAV
 *   2. fread 头 256B → 解析 RIFF + 搜索 data chunk
 *   3. 预加载 ping/pong 两帧到 DMA (preload_data)
 *   4. i2s_channel_enable → DMA 从 ping 开始播放
 *   5. 主循环交替填充 pong/ping → i2s_channel_write
 *   6. fclose + i2s_channel_disable
 * ================================================================ */
static void audio_playback(play_request_t *req) {
    /* ── 1. 构建 SD 卡路径 ── */
    char path[64];
    snprintf(path, sizeof(path), "/audio/%s.wav", s_names[req->id]);

    /* ── 2. 打开文件 ── */
    FILE *f = sdcard_fopen(path);
    if (!f) {
        ESP_LOGW(TAG, "[%s] TF 卡文件未找到: %s", s_names[req->id], path);
        return;
    }

    /* ── 3. 读取 WAV 头部 (256B 覆盖标准头 + LIST/INFO 扩展) ── */
    uint8_t header[256];
    size_t hdr_read = fread(header, 1, sizeof(header), f);
    if (hdr_read < sizeof(wav_fmt_t)) {
        ESP_LOGE(TAG, "[%s] WAV 头部读取失败", s_names[req->id]);
        fclose(f);
        return;
    }

    /* ── 4. 校验 RIFF ── */
    if (header[0] != 'R' || header[1] != 'I' || header[2] != 'F' ||
        header[3] != 'F') {
        ESP_LOGE(TAG, "[%s] 无效 WAV 文件", s_names[req->id]);
        fclose(f);
        return;
    }

    /* ── 5. 搜索 data chunk ── */
    const uint8_t *data_ptr = NULL;
    uint32_t data_size = 0;
    size_t data_mark =
        wav_find_data(header, header + hdr_read, &data_ptr, &data_size);
    if (!data_mark || !data_ptr || data_size == 0) {
        ESP_LOGE(TAG, "[%s] 未找到 data chunk", s_names[req->id]);
        fclose(f);
        return;
    }

    /* ── 6. 定位到 data chunk 负载起始 ── */
    size_t data_file_pos = (size_t)(data_ptr - header);
    fseek(f, data_file_pos, SEEK_SET);

    s_playing = true;
    s_stop_req = false;

    size_t remaining = data_size;
    int buf_idx = 0;       // 0=ping, 1=pong
    bool i2s_active = false;

    /* ── 7. Ping-pong 预加载: 填满 DMA TX FIFO 再启动 ── */
    for (int pre = 0; pre < 2 && remaining > 0; pre++) {
        size_t chunk = (remaining > MONO_BUF_SZ) ? MONO_BUF_SZ : remaining;
        size_t actual = fread(s_mono_buf, 1, chunk, f);
        if (actual == 0)
            break;

        size_t slen = mono_to_stereo(s_mono_buf, actual, VOLUME_GAIN_PCT,
                                     s_audio_buf[pre]);
        size_t loaded = 0;
        i2s_channel_preload_data(s_i2s_handle, s_audio_buf[pre], slen,
                                 &loaded);
        remaining -= actual;
        buf_idx = pre ^ 1;
    }

    /* ── 8. 启动 I2S — DMA 从预加载的两帧开始无间断播放 ── */
    if (remaining < data_size) { // 至少预加载了数据
        ESP_ERROR_CHECK(i2s_channel_enable(s_i2s_handle));
        i2s_active = true;
    }

    /* ── 9. 主播放循环: 交替填充 ping/pong ── */
    uint32_t play_start_ticks = xTaskGetTickCount();
    while (remaining > 0) {
        if (s_stop_req) {
            ESP_LOGI(TAG, "■ %s — 外部停止", s_names[req->id]);
            goto done;
        }

        /* 超时保护: 最长提示音 ~3s, 10s 未播完强制终止 */
        if ((xTaskGetTickCount() - play_start_ticks) > pdMS_TO_TICKS(10000)) {
            ESP_LOGE(TAG, "⚠ [%s] 播放超时 10s, 强制终止", s_names[req->id]);
            goto done;
        }

        /* 检查更高优先级待处理请求 */
        play_request_t peek;
        if (xQueuePeek(s_queue, &peek, 0) == pdTRUE) {
            if (peek.priority > req->priority) {
                ESP_LOGI(TAG, "⏏ %s → 被 prio=%d 打断", s_names[req->id],
                         peek.priority);
                goto done;
            }
        }

        /* 从 SD 卡读下一块 */
        size_t chunk = (remaining > MONO_BUF_SZ) ? MONO_BUF_SZ : remaining;
        size_t actual = fread(s_mono_buf, 1, chunk, f);
        if (actual == 0)
            break;

        /* mono → stereo (写入当前 ping/pong 帧) */
        size_t slen = mono_to_stereo(s_mono_buf, actual, VOLUME_GAIN_PCT,
                                     s_audio_buf[buf_idx]);

        /* 写入 I2S — 阻塞直到 DMA 开始消费此帧 */
        size_t offset = 0;
        while (offset < slen) {
            if (s_stop_req)
                goto done;

            size_t written = 0;
            esp_err_t ret = i2s_channel_write(s_i2s_handle,
                                              s_audio_buf[buf_idx] + offset,
                                              slen - offset, &written,
                                              portMAX_DELAY);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "I2S 写入失败: %s", esp_err_to_name(ret));
                goto done;
            }
            offset += written;
        }

        remaining -= actual;
        buf_idx ^= 1; // ping-pong 翻转
    }

    /* 数据全部送入 DMA, 等待排空 */
    vTaskDelay(pdMS_TO_TICKS(500));

done:
    if (i2s_active) {
        i2s_channel_disable(s_i2s_handle);
    }
    fclose(f);
    s_playing = false;
    s_stop_req = false;
    if (s_play_done) {
        xSemaphoreGive(s_play_done);
    }
}

/* ================================================================
 * 后台音频播放任务
 * ================================================================ */
static void audio_task(void *arg) {
    (void)arg;
    ESP_LOGI(TAG, "音频任务启动");

    play_request_t req;
    while (1) {
        if (xQueueReceive(s_queue, &req, portMAX_DELAY) == pdTRUE) {
            audio_playback(&req);
        }
    }
}

/* ================================================================
 * 公共 API
 * ================================================================ */

void audio_init(const audio_pins_t *pins, uint32_t sample_rate) {
    if (s_i2s_handle != NULL) {
        ESP_LOGW(TAG, "已初始化");
        return;
    }

    int bclk = (pins && pins->bclk_pin) ? pins->bclk_pin : DEFAULT_BCLK;
    int lrc = (pins && pins->lrc_pin) ? pins->lrc_pin : DEFAULT_LRC;
    int dout = (pins && pins->dout_pin) ? pins->dout_pin : DEFAULT_DOUT;
    uint32_t rate = (sample_rate > 0) ? sample_rate : DEFAULT_SAMPLE_RATE;

    ESP_LOGI(TAG, "初始化 I2S: BCLK=%d LRC=%d DOUT=%d SR=%" PRIu32, bclk, lrc,
             dout, rate);

    /* ── I2S Channel (DMA 缓冲 16×64 帧 = 4KB, 容忍 SD 卡延迟) ── */
    i2s_chan_config_t chan_cfg =
        I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = 16; // 8 → 16, 翻倍 DMA 缓冲深度
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &s_i2s_handle, NULL));

    /* ── I2S 标准立体声 (MAX98357A 要求) ── */
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(rate),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg =
            {
                .mclk = I2S_GPIO_UNUSED,
                .bclk = bclk,
                .ws = lrc,
                .dout = dout,
                .din = I2S_GPIO_UNUSED,
                .invert_flags = {false, false, false},
            },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_i2s_handle, &std_cfg));
    /* I2S 在实际播放时 enable，播放完 disable — 避免 DMA 空转噪声 */

    /* ── 播放队列 ── */
    s_queue = xQueueCreate(PLAY_QUEUE_LEN, sizeof(play_request_t));
    assert(s_queue != NULL);

    /* ── 同步信号量 (初始为 1, 表示"空闲") ── */
    s_play_done = xSemaphoreCreateBinary();
    assert(s_play_done != NULL);
    xSemaphoreGive(s_play_done); /* 初始可用 */

    /* ── 后台任务 ── */
    BaseType_t ret = xTaskCreate(audio_task, "audio", AUDIO_TASK_STACK, NULL,
                                 AUDIO_TASK_PRIO, &s_task);
    assert(ret == pdPASS);

    ESP_LOGI(TAG, "音频播放器就绪 (TF 卡流式 + 双缓冲)");
}

void audio_play(sound_id_t id) { audio_play_prio(id, AUDIO_PRIO_NORMAL); }

void audio_play_prio(sound_id_t id, audio_priority_t priority) {
    if (id >= SOUND_COUNT) {
        ESP_LOGE(TAG, "无效 ID: %d", id);
        return;
    }
    if (s_queue == NULL) {
        ESP_LOGE(TAG, "未初始化");
        return;
    }

    /* 若未显式指定优先级，用默认值 */
    if (priority == AUDIO_PRIO_NORMAL) {
        audio_priority_t def = s_priorities[id];
        if (def != 0)
            priority = def;
    }

    play_request_t req = {.id = id, .priority = priority};

    /* 非阻塞发送 — 队列满则丢弃 */
    if (xQueueSend(s_queue, &req, 0) != pdTRUE) {
        /* 若为 CRITICAL 优先级，强制清空队列放入 */
        if (priority == AUDIO_PRIO_CRITICAL) {
            xQueueReset(s_queue); // 清空
            xQueueSend(s_queue, &req, 0);
            ESP_LOGW(TAG, "‼ 关键告警 — 清空队列插入");
        } else {
            ESP_LOGW(TAG, "队列满 — 丢弃 %s", s_names[id]);
        }
    }
}

bool audio_is_playing(void) { return s_playing; }

void audio_stop(void) {
    if (!s_playing)
        return;
    s_stop_req = true;
    /* 短暂等待播放循环看到标志 */
    vTaskDelay(pdMS_TO_TICKS(100));
}

bool audio_play_wait(sound_id_t id, uint32_t ms_timeout) {
    if (id >= SOUND_COUNT)
        return false;
    if (!s_queue || !s_play_done)
        return false;

    /* 先取信号量 (确保前一次已完成) */
    xSemaphoreTake(s_play_done, 0);

    audio_play(id);

    /* 等待后台任务取出请求、开始播放并完成 */
    TickType_t ticks =
        (ms_timeout == 0) ? portMAX_DELAY : pdMS_TO_TICKS(ms_timeout);
    return xSemaphoreTake(s_play_done, ticks) == pdTRUE;
}

const char *audio_sound_name(sound_id_t id) {
    if (id >= SOUND_COUNT)
        return "?";
    return s_names[id];
}

const char *audio_sound_name_cn(sound_id_t id) {
    if (id >= SOUND_COUNT)
        return "?";
    return s_names_cn[id];
}

const char *audio_sound_name_en(sound_id_t id) { return audio_sound_name(id); }
