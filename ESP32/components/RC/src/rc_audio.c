/* SPDX-License-Identifier: Unlicense */
/*
 * ESP32 手柄提示音播放器 — 实现
 *
 * 依赖:
 *   - MAX98357A I2S 功放 (BCLK=19, LRC=20, DIN=37)
 *   - I2S 标准 Philips 立体声模式 (MAX98357A 要求)
 *   - 16bit/16000Hz/mono PCM WAV 输入 → 立体声输出
 *
 * 播放模型:
 *   - 后台 FreeRTOS 任务 + 队列 (深度 4)
 *   - 非阻塞: audio_play() 立即返回
 *   - 打断: 高优先级请求可中断当前播放
 *   - 丢弃: 队列满时新请求被丢弃
 */

#include "rc_audio.h"
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
#define STEREO_BUF_SZ (MONO_BUF_SZ * 2) // 立体声输出块大小
#define PLAY_QUEUE_LEN 4                // 播放队列深度
#define AUDIO_TASK_STACK 4096
#define AUDIO_TASK_PRIO 5

/* 音量增益 */
#define VOLUME_GAIN_PCT 50

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
 * 返回 data 开始位置 (相对于 start) 和 data size
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
            return (size_t)(p - start); // header 结束偏移 (data chunk 之后)
        }
        /* 跳过当前 chunk: 4字节ID + 4字节size + payload */
        uint32_t chunk_size = (uint32_t)p[4] | ((uint32_t)p[5] << 8) |
                              ((uint32_t)p[6] << 16) | ((uint32_t)p[7] << 24);
        p += 8 + chunk_size;
    }
    return 0; // 未找到
}

/* ================================================================
 * 嵌入的音频数据 —— 符号声明
 * 说明：ESP-IDF 的 EMBED_FILES 根据文件 NAME 生成符号，
 *       audio/armed.wav → _binary_armed_wav_start/end
 *       MAKE_C_IDENTIFIER 将 '.' 替换为 '_'
 * ================================================================ */
#define SOUND_DECL(name)                                                       \
    extern const uint8_t _binary_##name##_wav_start[];                         \
    extern const uint8_t _binary_##name##_wav_end[]

SOUND_DECL(hello);
SOUND_DECL(armed);
SOUND_DECL(modesw);
SOUND_DECL(fpvmod);
SOUND_DECL(btmod);
SOUND_DECL(wifimd);
SOUND_DECL(usbmod);
SOUND_DECL(xboxmod);
SOUND_DECL(wificon);
SOUND_DECL(wifidcn);
SOUND_DECL(btcon);
SOUND_DECL(btdcn);
SOUND_DECL(lowbatt);
SOUND_DECL(lowbat);
SOUND_DECL(rssi_org);
SOUND_DECL(rssi_red);
SOUND_DECL(telemok);
SOUND_DECL(telemko);
SOUND_DECL(sensorko);
SOUND_DECL(modelpwr);
SOUND_DECL(thralert);
SOUND_DECL(swalert);
SOUND_DECL(inactiv);
SOUND_DECL(rfmod);
SOUND_DECL(binding);
SOUND_DECL(bindfail);
SOUND_DECL(lock);
/* ================================================================
 * 音频数据查找表
 * ================================================================ */
typedef struct {
    const uint8_t *start;
    const uint8_t *end;
} sound_data_t;

/* 格式: SOUND_ENTRY(枚举名, 小写文件名) */
#define SOUND_ENTRY(en, fn)                                                    \
    [en] = {                                                                   \
        .start = _binary_##fn##_wav_start,                                     \
        .end = _binary_##fn##_wav_end,                                         \
    }

static const sound_data_t s_sounds[SOUND_COUNT] = {
    SOUND_ENTRY(SOUND_HELLO, hello),
    SOUND_ENTRY(SOUND_ARMED, armed),
    SOUND_ENTRY(SOUND_MODESW, modesw),
    SOUND_ENTRY(SOUND_FPVMOD, fpvmod),
    SOUND_ENTRY(SOUND_BTMOD, btmod),
    SOUND_ENTRY(SOUND_WIFIMD, wifimd),
    SOUND_ENTRY(SOUND_USBMOD, usbmod),
    SOUND_ENTRY(SOUND_XBOXMOD, xboxmod),
    SOUND_ENTRY(SOUND_WIFICON, wificon),
    SOUND_ENTRY(SOUND_WIFIDCN, wifidcn),
    SOUND_ENTRY(SOUND_BTCON, btcon),
    SOUND_ENTRY(SOUND_BTDCN, btdcn),
    SOUND_ENTRY(SOUND_LOWBATT, lowbatt),
    SOUND_ENTRY(SOUND_LOWBAT, lowbat),
    SOUND_ENTRY(SOUND_RSSI_ORG, rssi_org),
    SOUND_ENTRY(SOUND_RSSI_RED, rssi_red),
    SOUND_ENTRY(SOUND_TELEMOK, telemok),
    SOUND_ENTRY(SOUND_TELEMKO, telemko),
    SOUND_ENTRY(SOUND_SENSORKO, sensorko),
    SOUND_ENTRY(SOUND_MODELPWR, modelpwr),
    SOUND_ENTRY(SOUND_THRALERT, thralert),
    SOUND_ENTRY(SOUND_SWALERT, swalert),
    SOUND_ENTRY(SOUND_INACTIV, inactiv),
    SOUND_ENTRY(SOUND_RFMOD, rfmod),
    SOUND_ENTRY(SOUND_BINDING, binding),
    SOUND_ENTRY(SOUND_BINDFAIL, bindfail),
    SOUND_ENTRY(SOUND_LOCKED, lock),
};

#undef SOUND_ENTRY

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
    [SOUND_LOCKED] = "lock",
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

/* 静态缓冲区 — BSS 段, 不占栈 */
static uint8_t s_stereo_buf[STEREO_BUF_SZ];

/* ================================================================
 * mono → stereo 转换 + 增益 + 硬限幅
 * 输入:  [S0l,S0h, S1l,S1h, ...]  (16bit mono PCM)
 * 输出:  [S0l,S0h, S0l,S0h, S1l,S1h, S1l,S1h, ...] (16bit stereo)
 * gain:  100=原始, 150=+3.5dB, 200=+6dB
 * ================================================================ */
static size_t mono_to_stereo(const uint8_t *mono, size_t mono_len,
                             uint8_t gain_pct) {
    size_t samples = mono_len / 2;
    size_t out_len = samples * 4;

    if (out_len > STEREO_BUF_SZ) {
        samples = STEREO_BUF_SZ / 4;
        out_len = samples * 4;
    }

    const int16_t *in = (const int16_t *)mono;
    int16_t *out = (int16_t *)s_stereo_buf;

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
 * ================================================================ */
static void audio_playback(play_request_t *req) {
    const sound_data_t *sd = &s_sounds[req->id];

    /* 校验 WAV */
    if (sd->start[0] != 'R' || sd->start[1] != 'I' || sd->start[2] != 'F' ||
        sd->start[3] != 'F') {
        ESP_LOGE(TAG, "[%s] 无效 WAV 文件", s_names[req->id]);
        return;
    }

    /* 搜索 data chunk (兼容 LIST/INFO 等扩展块) */
    const uint8_t *mono_data = NULL;
    uint32_t data_size = 0;
    size_t hdr_end = wav_find_data(sd->start, sd->end, &mono_data, &data_size);
    if (!hdr_end || !mono_data || data_size == 0) {
        ESP_LOGE(TAG, "[%s] 未找到 data chunk", s_names[req->id]);
        return;
    }

    const wav_fmt_t *fmt = (const wav_fmt_t *)sd->start;
    size_t mono_total = data_size;
    size_t mono_offset = 0;
    float duration = (float)data_size / fmt->byte_rate;

    ESP_LOGI(TAG, "▶ %s (%s) prio=%d %.2fs", s_names[req->id],
             s_names_cn[req->id], req->priority, duration);

    s_playing = true;
    s_stop_req = false;

    /* 预加载首批数据到 DMA */
    size_t preload_src = (mono_total < MONO_BUF_SZ) ? mono_total : MONO_BUF_SZ;
    if (preload_src > 0) {
        size_t pre_stereo =
            mono_to_stereo(mono_data, preload_src, VOLUME_GAIN_PCT);
        size_t loaded = 0;
        i2s_channel_preload_data(s_i2s_handle, s_stereo_buf, pre_stereo,
                                 &loaded);
    }
    mono_offset = preload_src;

    /* 启动 I2S 开始播放 */
    ESP_ERROR_CHECK(i2s_channel_enable(s_i2s_handle));

    /* ─── 主播放循环 ─── */
    while (mono_offset < mono_total) {
        /* 检查外部停止请求 */
        if (s_stop_req) {
            ESP_LOGI(TAG, "■ %s — 外部停止", s_names[req->id]);
            goto done;
        }

        /* 检查是否有更高优先级的请求待处理
         * xQueuePeek 不消耗队列元素，只查看 */
        play_request_t peek;
        if (xQueuePeek(s_queue, &peek, 0) == pdTRUE) {
            if (peek.priority > req->priority) {
                ESP_LOGI(TAG, "⏏ %s → 被 prio=%d 打断", s_names[req->id],
                         peek.priority);
                goto done;
            }
        }

        /* 确定本次转换量 */
        size_t mono_chunk = MONO_BUF_SZ;
        if (mono_offset + mono_chunk > mono_total) {
            mono_chunk = mono_total - mono_offset;
        }

        /* mono → stereo */
        size_t stereo_len = mono_to_stereo(mono_data + mono_offset, mono_chunk,
                                           VOLUME_GAIN_PCT);

        /* 写入 I2S */
        size_t offset = 0;
        while (offset < stereo_len) {
            if (s_stop_req)
                goto done;

            size_t written = 0;
            esp_err_t ret =
                i2s_channel_write(s_i2s_handle, s_stereo_buf + offset,
                                  stereo_len - offset, &written, portMAX_DELAY);

            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "I2S 写入失败: %s", esp_err_to_name(ret));
                goto done;
            }
            offset += written;
        }

        mono_offset += mono_chunk;
    }

    /* 数据全部送入 DMA，等待排空 */
    ESP_LOGI(TAG, "□ %s — 播完", s_names[req->id]);
    vTaskDelay(pdMS_TO_TICKS(500));

done:
    /* 停止 I2S 防止 DMA 空转输出噪声 */
    i2s_channel_disable(s_i2s_handle);
    s_playing = false;
    s_stop_req = false;

    /* 通知等待者 (如果有) */
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

    /* ── I2S Channel ── */
    i2s_chan_config_t chan_cfg =
        I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
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

    ESP_LOGI(TAG, "音频播放器就绪");
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
