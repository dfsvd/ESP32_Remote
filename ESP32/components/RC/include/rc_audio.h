/* SPDX-License-Identifier: Unlicense */
/*
 * ESP32 手柄提示音播放器
 * 基于 I2S + MAX98357A 的音频播报系统
 *
 * 特性:
 *   - 后台任务驱动, 非阻塞播放
 *   - 4 级优先级, 高优先级可打断低优先级
 *   - 队列管理, 请求满则丢弃
 *   - 支持查询播放状态、强制停止
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========== 提示音枚举 ========== */
typedef enum {
    SOUND_HELLO = 0, // 开机
    SOUND_ARMED,     // 已解锁
    SOUND_MODESW,    // 模式切换
    SOUND_FPVMOD,    // FPV 模式
    SOUND_BTMOD,     // 蓝牙模式
    SOUND_WIFIMD,    // WiFi 模式
    SOUND_USBMOD,    // USB 模式
    SOUND_XBOXMOD,   // Xbox 模式
    SOUND_WIFICON,   // WiFi 连接
    SOUND_WIFIDCN,   // WiFi 断开
    SOUND_BTCON,     // 蓝牙连接
    SOUND_BTDCN,     // 蓝牙断开
    SOUND_LOWBATT,   // 遥控器电压低
    SOUND_LOWBAT,    // 电池电压低
    SOUND_RSSI_ORG,  // 射频信号弱
    SOUND_RSSI_RED,  // 射频信号危险
    SOUND_TELEMOK,   // 回传恢复
    SOUND_TELEMKO,   // 回传丢失
    SOUND_SENSORKO,  // 传感器丢失
    SOUND_MODELPWR,  // 接收机未关闭
    SOUND_THRALERT,  // 警告:油门不在最低
    SOUND_SWALERT,   // 警告:开关不在初始位置
    SOUND_INACTIV,   // 长时间无操作
    SOUND_RFMOD,     // 射频模式
    SOUND_BINDING,   // 正在连接接收机
    SOUND_BINDFAIL,  // 连接失败
    SOUND_LOCKED,    // 已锁定
    SOUND_COUNT,
} sound_id_t;

/* ========== 播放优先级 ==========
 *   CRITICAL > HIGH > NORMAL > LOW
 *   优先级高的可打断正在播放的低优先级音
 */
typedef enum {
    AUDIO_PRIO_LOW = 0,
    AUDIO_PRIO_NORMAL = 1,
    AUDIO_PRIO_HIGH = 2,
    AUDIO_PRIO_CRITICAL = 3,
} audio_priority_t;

/* ========== I2S 引脚配置 ========== */
typedef struct {
    int bclk_pin; // BCLK (默认 GPIO11)
    int lrc_pin;  // LRC/WS (默认 GPIO12)
    int dout_pin; // DIN (默认 GPIO13)
} audio_pins_t;

/* ========== 公共 API ========== */

/**
 * @brief 初始化音频播放器
 * @param pins        I2S 引脚, NULL=使用默认 (BCLK=19, LRC=20, DOUT=37)
 * @param sample_rate 采样率, 0=使用默认 16000 Hz
 * @note 创建后台播放任务, 初始化 I2S
 */
void audio_init(const audio_pins_t *pins, uint32_t sample_rate);

/**
 * @brief 播放提示音 (默认 NORMAL 优先级)
 */
void audio_play(sound_id_t id);

/**
 * @brief 以指定优先级播放提示音
 * @param id       提示音 ID
 * @param priority 优先级
 * @note 高优先级可打断低优先级正在播放的音
 */
void audio_play_prio(sound_id_t id, audio_priority_t priority);

/**
 * @brief 是否正在播放
 * @return true = 正在播放
 */
bool audio_is_playing(void);

/**
 * @brief 强制停止当前播放
 * @note 高优先级请求到达时会自动停止低优先级
 */
void audio_stop(void);

/**
 * @brief 阻塞播放提示音，等待播放完成后再返回
 * @param id       提示音 ID
 * @param ms_timeout 超时时间 (ms)，0 = 无限等待
 * @return true = 正常播完, false = 超时
 * @note 内部使用信号量同步，适合需要确保语音播完再继续的场景
 */
bool audio_play_wait(sound_id_t id, uint32_t ms_timeout);

/**
 * @brief 获取提示音中文名称
 */
const char *audio_sound_name_cn(sound_id_t id);

/**
 * @brief 获取提示音英文名称
 */
const char *audio_sound_name_en(sound_id_t id);

/**
 * @brief 获取枚举名 (C 标识符风格)
 */
const char *audio_sound_name(sound_id_t id);

#ifdef __cplusplus
}
#endif
