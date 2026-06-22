
/* =========================================================================
 * FPV 遥控器 — 主入口
 *
 * 功能概要：
 *   - SA 长按 + 右摇杆方向选择启动模式 (USB Xbox / USB MSC / BLE / WiFi /
 * 透穿调参)
 *   - 驱动 CRSF 协议栈与高频头通信
 *   - 自动对频状态机
 *   - 摇杆→CRSF 通道同步
 *   - USB HID 手柄输出 (含 Xbox 模式)
 *   - USB MSC U盘模式 (TF 卡直读)
 * ========================================================================= */

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "rc_audio.h"
#include "rc_sdcard.h"
#include "rc_ble.h"
#include "rc_bridge.h"
#include "rc_crsf.h"
#include "rc_led.h"
#include "rc_read.h"
#include "rc_usb.h"
#include "rc_usb_host.h"
#include "rc_usb_msc.h"
#include "rc_wf.h"
#include "tinyusb.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* =========================================================================
 * 常量定义
 * ========================================================================= */

static const char *TAG = "FPV_RC";

#define CRSF_LINK_MODE_UART_FULL_DUPLEX 0
#define CRSF_LINK_MODE_UART_HALF_DUPLEX 1
#define CRSF_LINK_MODE CRSF_LINK_MODE_UART_FULL_DUPLEX
#define CRSF_UART_PORT UART_NUM_1
#define CRSF_UART_TX_PIN GPIO_NUM_17
#define CRSF_UART_RX_PIN GPIO_NUM_16
#define CRSF_UART_HALF_DUPLEX_PIN GPIO_NUM_17
#define CRSF_UART_INVERTED 0
#define CRSF_PARAM_TYPE_COMMAND 0x0D
#define FALLBACK_BIND_PARAM_ID 18
#define AUTO_BIND_INTERVAL_MS 2000U
#define AUTO_BIND_LINK_STABLE_MS 1200U
#define BIND_TIMEOUT_MS 30000U // 连接接收机超时

// ---- 开机模式切换 ----
#define BOOT_KEY_HOLD_MS 3000             // 按键长按判定时间
#define BOOT_STICK_SELECT_TIMEOUT_MS 5000 // 摇杆选择超时
#define BOOT_STICK_UP_THRESH 1300         // 右摇杆上打阈值 (<此值)
#define BOOT_STICK_DOWN_THRESH 1700       // 右摇杆下打阈值 (>此值)
#define BOOT_STICK_RIGHT_THRESH 1700      // 右摇杆右打阈值 (>此值)
#define BOOT_STICK_LEFT_THRESH 1300       // 右摇杆左打阈值 (<此值)
#define BOOT_DEBOUNCE_MS 80               // 按键消抖时间

// ---- RSSI 信号阈值 ----
#define RSSI_WARN_ORG 120           // CRSF RSSI < 此值 → 播"信号弱"
#define RSSI_WARN_RED 90            // CRSF RSSI < 此值 → 播"信号危险"
#define RSSI_ORG_INTERVAL_MS 30000U // 信号弱播报间隔
#define RSSI_RED_INTERVAL_MS 10000U // 信号危险播报间隔

/* =========================================================================
 * 开机模式枚举
 * ========================================================================= */
typedef enum {
    BOOT_MODE_RF = 0,       // 纯射频（默认兜底）
    BOOT_MODE_USB_FPV,      // USB 标准 HID 手柄
    BOOT_MODE_USB_XBOX,     // USB Xbox 360 手柄
    BOOT_MODE_BLE_FPV,      // 蓝牙 FPV HID 手柄
    BOOT_MODE_WIFI,         // WiFi AP + WebSocket
    BOOT_MODE_PASSTHROUGH,  // BLE NUS 透穿调参 (BLE UART ↔ CRSF MSP)
    BOOT_MODE_USB_MSC,      // USB U盘 (MSC 存储)
    BOOT_MODE_UNKNOWN = -1, // 用户未做选择
} boot_mode_t;

static const char *boot_mode_name(boot_mode_t m) {
    switch (m) {
    case BOOT_MODE_RF:
        return "纯射频";
    case BOOT_MODE_USB_FPV:
        return "USB FPV";
    case BOOT_MODE_USB_XBOX:
        return "USB Xbox";
    case BOOT_MODE_BLE_FPV:
        return "蓝牙 FPV";
    case BOOT_MODE_WIFI:
        return "WiFi AP";
    case BOOT_MODE_PASSTHROUGH:
        return "透穿调参";
    case BOOT_MODE_USB_MSC:
        return "USB U盘";
    default:
        return "未知";
    }
}

static boot_mode_t get_default_mode(void) {
    // TODO: 将来从 NVS 读取上次保存的模式
    // return nvs_read_boot_mode(BOOT_MODE_WIFI);
    return BOOT_MODE_WIFI;
}

/* =========================================================================
 * 全局状态
 * ========================================================================= */

/**
 * @brief 全局摇杆状态，供 USB / BLE / WiFi / CRSF 模块共享
 * @note  初始值 1500(中位) / 1000(低位)，防止未启用的通道发送 0 导致
 *         Windows 丢弃整个 HID 报文
 */
static fpv_joystick_report_t joy = {
    .roll = 1500,
    .pitch = 1500,
    .throttle = 1000,
    .yaw = 1500,
    .aux1 = 1000,
    .aux2 = 1000,
    .aux3 = 1000,
    .aux4 = 1000,
    .sw1 = 1000,
    .sw2 = 1000,
    .sw3 = 1000,
    .sw4 = 1000,
    .sw5 = 1000,
    .sw6 = 1000,
    .sw7 = 1000,
    .sw8 = 1000,
};

/**
 * @brief 自动对频状态机上下文
 */
struct auto_bind_ctx {
    bool active;               /**< 是否处于自动对频模式                   */
    bool complete_logged;      /**< 是否已输出"对频成功"日志                */
    uint32_t last_try_ms;      /**< 上次发送 bind 命令的时间戳 (ms)         */
    uint32_t link_up_since_ms; /**< 链路建立时刻的时间戳 (ms)，0=未上线      */
    uint32_t last_wait_log_ms; /**< 上次输出等待日志的时间戳 (ms)            */
};

/* =========================================================================
 * 工具函数
 * ========================================================================= */

/**
 * @brief 将输入字符串清洗为可打印 ASCII，特殊控制码转为可读标签
 * @note  0xC0 → ":Lo", 0xC1 → ":Hi", 其他不可打印字节 → "\xNN"
 * @param dst      目标缓冲区
 * @param dst_size 目标缓冲区大小（含 '\0'）
 * @param src      源字符串，为 NULL 时仅清空 dst
 */
static void sanitize_ascii(char *dst, size_t dst_size, const char *src) {
    size_t out = 0;

    if (!dst || dst_size == 0)
        return;
    dst[0] = '\0';
    if (!src)
        return;

    for (size_t i = 0; src[i] != '\0' && out + 1 < dst_size; ++i) {
        unsigned char c = (unsigned char)src[i];
        if (c == 0xC0) {
            int written = snprintf(dst + out, dst_size - out, ":Lo");
            if (written <= 0 || (size_t)written >= dst_size - out)
                break;
            out += (size_t)written;
            continue;
        }
        if (c == 0xC1) {
            int written = snprintf(dst + out, dst_size - out, ":Hi");
            if (written <= 0 || (size_t)written >= dst_size - out)
                break;
            out += (size_t)written;
            continue;
        }
        if (c >= 32 && c <= 126) {
            dst[out++] = (char)c;
            continue;
        }

        int written = snprintf(dst + out, dst_size - out, "\\x%02X", c);
        if (written <= 0 || (size_t)written >= dst_size - out)
            break;
        out += (size_t)written;
    }

    dst[out] = '\0';
}

/**
 * @brief 将 CRSF 菜单类型编码转换为中文名称
 * @param type CRSF 菜单项类型枚举值
 *             (0x09=选项, 0x0A=文本, 0x0B=目录, 0x0C=信息, 0x0D=命令)
 * @return 类型的中文描述字符串
 */
static const char *crsf_menu_type_name(uint8_t type) {
    switch (type) {
    case 0x09:
        return "选项";
    case 0x0A:
        return "文本";
    case 0x0B:
        return "目录";
    case 0x0C:
        return "信息";
    case 0x0D:
        return "命令";
    default:
        return "常规";
    }
}

/**
 * @brief 对字符串进行大小写不敏感的 token 包含判断
 * @param text  被搜索的源字符串
 * @param token 要查找的子串
 * @return true  text 包含 token (忽略大小写)
 * @return false text 为空、token 为空或未找到
 */
static bool contains_case_insensitive(const char *text, const char *token) {
    if (!text || !token || token[0] == '\0')
        return false;

    size_t token_len = strlen(token);
    size_t text_len = strlen(text);
    if (token_len > text_len)
        return false;

    for (size_t i = 0; i + token_len <= text_len; ++i) {
        bool matched = true;
        for (size_t j = 0; j < token_len; ++j) {
            char a = (char)tolower((unsigned char)text[i + j]);
            char b = (char)tolower((unsigned char)token[j]);
            if (a != b) {
                matched = false;
                break;
            }
        }
        if (matched)
            return true;
    }

    return false;
}

/* =========================================================================
 * CRSF 操作函数
 * ========================================================================= */

/**
 * @brief 打印完整 CRSF 状态快照到日志
 * @param db 指向 CRSF 全局状态结构体的指针，为 NULL 时直接返回
 */
static void print_crsf_state_snapshot(const crsf_state_t *db) {
    if (!db)
        return;

    ESP_LOGW(TAG, "================ 📊 CRSF 全局快照 ================");
    ESP_LOGI(TAG, "[状态] 握手:%s | 飞机:%s | RSSI:-%d | LQ:%u | SNR:%d",
             db->is_ready ? "是" : "否", db->is_linked ? "是" : "否", db->rssi,
             db->lq, db->snr);
    ESP_LOGI(TAG, "[加载] %d / %d (有效/总计)", db->loaded_params,
             db->total_params);
    ESP_LOGW(TAG, "---------------- 📂 菜单列表 ----------------");

    for (int i = 0; i < CRSF_MAX_MENU_ITEMS; i++) {
        if (db->menu[i].id == 0 || !db->menu[i].is_valid)
            continue;

        const char *n = db->menu[i].name;
        const char *extra = db->menu[i].options[0] ? db->menu[i].options : "-";
        const char *type_name = crsf_menu_type_name(db->menu[i].type);
        char safe_name[160];
        char safe_extra[768];

        sanitize_ascii(safe_name, sizeof(safe_name), n);
        sanitize_ascii(safe_extra, sizeof(safe_extra), extra);

        if (db->menu[i].type == 0x09) {
            ESP_LOGI(TAG,
                     " [%02d] <%s> 值:%-3d | 父:%-2d | 名称:%-18s | 选项:%s",
                     db->menu[i].id, type_name, db->menu[i].value,
                     db->menu[i].parent_id, safe_name, safe_extra);
        } else if (db->menu[i].type == 0x0C || db->menu[i].type == 0x0A) {
            ESP_LOGI(TAG,
                     " [%02d] <%s>        | 父:%-2d | 名称:%-18s | 内容:%s",
                     db->menu[i].id, type_name, db->menu[i].parent_id,
                     safe_name, safe_extra);
        } else if (db->menu[i].type == 0x0D) {
            ESP_LOGI(TAG,
                     " [%02d] <%s> 状态:%-3d | 父:%-2d | 名称:%-18s | 信息:%s",
                     db->menu[i].id, type_name, db->menu[i].value,
                     db->menu[i].parent_id, safe_name, safe_extra);
        } else {
            ESP_LOGI(TAG, " [%02d] <%s>        | 父:%-2d | 名称:%-18s",
                     db->menu[i].id, type_name, db->menu[i].parent_id,
                     safe_name);
        }
    }

    ESP_LOGW(TAG, "================================================");
}

/** 🔧 调试：打印全部 16 通道数值 */
static void print_joystick_snapshot(const fpv_joystick_report_t *r) {
    if (!r)
        return;
    ESP_LOGI(TAG,
             "CH1(roll)=%4d  CH2(pitch)=%4d  CH3(throttle)=%4d  CH4(yaw)=%4d",
             r->roll, r->pitch, r->throttle, r->yaw);
    ESP_LOGI(TAG, "CH5(aux1)=%4d  CH6(aux2)=%4d  CH7(aux3)=%4d  CH8(aux4)=%4d",
             r->aux1, r->aux2, r->aux3, r->aux4);
    ESP_LOGI(TAG, "CH9(sw1)=%4d  CH10(sw2)=%4d  CH11(sw3)=%4d  CH12(sw4)=%4d",
             r->sw1, r->sw2, r->sw3, r->sw4);
    ESP_LOGI(TAG, "CH13(sw5)=%4d  CH14(sw6)=%4d  CH15(sw7)=%4d  CH16(sw8)=%4d",
             r->sw5, r->sw6, r->sw7, r->sw8);
}

/**
 * @brief 将摇杆报告的 16 通道数据同步到 CRSF 协议栈
 * @note  通道映射: CH1=roll, CH2=pitch, CH3=throttle, CH4=yaw,
 *                   CH5-8=aux1-4, CH9-16=sw1-8
 * @param report 摇杆数据报告指针，为 NULL 时直接返回
 */
static void sync_joy_to_crsf(const fpv_joystick_report_t *report) {
    if (!report)
        return;

    crsf_set_channel(0, report->roll);
    crsf_set_channel(1, report->pitch);
    crsf_set_channel(2, report->throttle);
    crsf_set_channel(3, report->yaw);
    crsf_set_channel(4, report->aux1);
    crsf_set_channel(5, report->aux2);
    crsf_set_channel(6, report->aux3);
    crsf_set_channel(7, report->aux4);
    crsf_set_channel(8, report->sw1);
    crsf_set_channel(9, report->sw2);
    crsf_set_channel(10, report->sw3);
    crsf_set_channel(11, report->sw4);
    crsf_set_channel(12, report->sw5);
    crsf_set_channel(13, report->sw6);
    crsf_set_channel(14, report->sw7);
    crsf_set_channel(15, report->sw8);
}

/**
 * @brief 在 CRSF 菜单列表中查找名称含 "bind" 的命令项 ID
 * @param state CRSF 全局状态
 * @return 找到的命令项 ID，未找到返回 0
 */
static uint8_t find_bind_param_id(const crsf_state_t *state) {
    if (!state)
        return 0;

    for (int i = 0; i < CRSF_MAX_MENU_ITEMS; ++i) {
        const crsf_menu_item_t *item = &state->menu[i];
        if (!item->is_valid || item->type != CRSF_PARAM_TYPE_COMMAND)
            continue;
        if (contains_case_insensitive(item->name, "bind"))
            return item->id;
    }

    return 0;
}

/* =========================================================================
 * 开机模式检测
 * ========================================================================= */

/**
 * @brief 等待按键组合保持指定毫秒，任一松手即失败
 * @return true=保持成功, false=有按键松手
 */
static bool hold_keys_ms(uint32_t ms, bool check_sa, bool check_sd) {
    uint32_t start = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    while (1) {
        uint32_t elapsed =
            (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS) - start;
        if (elapsed >= ms)
            return true;
        if (check_sa && gpio_get_level(RC_SWITCH_SA_PIN) != 0)
            return false;
        if (check_sd && gpio_get_level(RC_SWITCH_SD_PIN) != 0)
            return false;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

/**
 * @brief 开机模式检测 — 只读 GPIO，不做任何初始化
 *
 * 决策树：
 *   SA 长按 3s        → 进入摇杆选择器:
 *                         上=USB_FPV, 下=BLE_FPV, 右=RF
 *                         SD 按下 → WIFI
 *                         超时 5s → UNKNOWN（调用方 fallback）
 *   SA 短按（<3s）    → UNKNOWN
 *   无操作            → UNKNOWN
 *
 * @return 用户主动选择的模式，或 BOOT_MODE_UNKNOWN（调用方 fallback
 * 到上次保存的模式）
 */
static boot_mode_t detect_boot_mode(void) {
    // 读取初始状态并消抖
    bool sa = (gpio_get_level(RC_SWITCH_SA_PIN) == 0);
    if (sa)
        vTaskDelay(pdMS_TO_TICKS(BOOT_DEBOUNCE_MS));

    // ---- SA 长按 → 摇杆选择 ----
    if (sa) {
        ESP_LOGI(TAG, "检测到 SA 按下，保持 %dms 进入模式选择...",
                 BOOT_KEY_HOLD_MS);
        if (hold_keys_ms(BOOT_KEY_HOLD_MS, true, false)) {
            audio_play(SOUND_MODESW);
            ESP_LOGI(TAG,
                     ">> 模式选择: 上=USB, 下=BLE, 右=U盘, 左=透穿, SD=WiFi "
                     "(超时 %dms)",
                     BOOT_STICK_SELECT_TIMEOUT_MS);
            uint32_t sel_start =
                (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
            while (1) {
                uint32_t elapsed =
                    (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS) -
                    sel_start;
                if (elapsed >= BOOT_STICK_SELECT_TIMEOUT_MS) {
                    ESP_LOGI(TAG, ">> 选择超时，回退上次模式");
                    audio_play(SOUND_RFMOD);
                    return BOOT_MODE_UNKNOWN;
                }

                // SD 按下 → WiFi
                if (gpio_get_level(RC_SWITCH_SD_PIN) == 0) {
                    ESP_LOGI(TAG, ">> SD 按下 → 进入 WiFi 模式");
                    audio_play_wait(SOUND_WIFIMD, 5000);
                    return BOOT_MODE_WIFI;
                }

                uint16_t pitch = joy.pitch;
                uint16_t roll = joy.roll;
                if (pitch < BOOT_STICK_UP_THRESH) {
                    ESP_LOGI(TAG, ">> 右摇杆上 → 进入 USB 模式");
                    audio_play_wait(SOUND_USBMOD, 5000);
                    // TODO: 可在此二次选择 FPV 还是 Xbox
                    return BOOT_MODE_USB_XBOX;
                }
                if (pitch > BOOT_STICK_DOWN_THRESH) {
                    ESP_LOGI(TAG, ">> 右摇杆下 → 进入 BLE 模式");
                    audio_play_wait(SOUND_BTMOD, 5000);
                    return BOOT_MODE_BLE_FPV;
                }
                if (roll > BOOT_STICK_RIGHT_THRESH) {
                    ESP_LOGI(TAG, ">> 右摇杆右 → 进入 USB U盘模式");
                    audio_play_wait(SOUND_MSCMOD, 5000);
                    return BOOT_MODE_USB_MSC;
                }
                if (roll < BOOT_STICK_LEFT_THRESH) {
                    ESP_LOGI(TAG, ">> 右摇杆左 → 进入透穿调参模式");
                    audio_play_wait(SOUND_PASMOD, 5000);
                    return BOOT_MODE_PASSTHROUGH;
                }
                vTaskDelay(pdMS_TO_TICKS(100));
            }
        }
        // SA 短按（未保持 3s）→ 未做选择
        ESP_LOGI(TAG, "SA 按钮未保持 %dms，回退上次模式", BOOT_KEY_HOLD_MS);
        return BOOT_MODE_UNKNOWN;
    }

    // ---- 无按键 → 未做选择 ----
    return BOOT_MODE_UNKNOWN;
}

/* =========================================================================
 * 自动对频状态机
 * ========================================================================= */

/**
 * @brief 自动对频状态机的单次轮询
 * @note  流程:
 *        1. 高频头未就绪 → 每 2s 输出等待日志
 *        2. 高频头就绪但未连接 → 周期性发送 Bind 命令
 *        3. 接收机上线 → 等待链路稳定后退出对频模式
 * @param state  CRSF 全局状态 (只读)
 * @param now_ms 当前系统时间 (ms)
 * @param ctx    对频上下文，由调用方持有，状态在函数内部更新
 */
static void poll_auto_bind(const crsf_state_t *state, uint32_t now_ms,
                           struct auto_bind_ctx *ctx) {
    if (!state || !ctx || !ctx->active)
        return;

    /* --- 状态 1: 接收机已连接，等待链路稳定 --- */
    if (state->is_linked) {
        if (ctx->link_up_since_ms == 0) {
            ctx->link_up_since_ms = now_ms;
            ESP_LOGI(TAG, "检测到链路上线，开始验证对频稳定性...");
        }
        if ((now_ms - ctx->link_up_since_ms) >= AUTO_BIND_LINK_STABLE_MS) {
            ctx->active = false;
            if (!ctx->complete_logged) {
                ESP_LOGI(TAG, "对频成功(链路稳定 %ums)，退出自动对频模式",
                         AUTO_BIND_LINK_STABLE_MS);
                ctx->complete_logged = true;
                led_set_mode(LED_MODE_CRSF_RF);
            }
        }
    }
    /* --- 状态 2: 高频头就绪，周期性发送 bind 命令 --- */
    else if (state->is_ready && state->total_params > 0 &&
             state->loaded_params == state->total_params) {
        ctx->link_up_since_ms = 0;
        if (now_ms - ctx->last_try_ms >= AUTO_BIND_INTERVAL_MS) {
            uint8_t bind_param_id = find_bind_param_id(state);
            if (bind_param_id == 0)
                bind_param_id = FALLBACK_BIND_PARAM_ID;

            crsf_write_menu_value(bind_param_id, 1);
            ctx->last_try_ms = now_ms;
            ESP_LOGI(TAG, "自动对频发包: PARAM_WRITE id=%u value=1 (间隔=%ums)",
                     bind_param_id, AUTO_BIND_INTERVAL_MS);
        }
    }
    /* --- 状态 3: 等待高频头就绪 --- */
    else if ((now_ms - ctx->last_wait_log_ms) >= 2000U) {
        ctx->last_wait_log_ms = now_ms;
        ESP_LOGI(TAG, "自动对频等待中: ready=%d menu=%u/%u linked=%d",
                 state->is_ready ? 1 : 0, state->loaded_params,
                 state->total_params, state->is_linked ? 1 : 0);
        ctx->link_up_since_ms = 0;
    }
}

/* =========================================================================
 * CRSF 回调
 * ========================================================================= */

/**
 * @brief CRSF 设备信息回调 — 高频头设备名称到达时触发
 * @note  当前仅打印日志，将来可通过屏幕 UI 直接展示设备名
 * @param name 高频头设备名称 (UTF-8)
 */
void my_crsf_device_info_callback(const char *name) {
    ESP_LOGI(TAG, "【应用层收到高频头信息】: %s", name);
}

/* =========================================================================
 * 主入口
 * ========================================================================= */

/**
 * @brief ESP32 应用程序主入口
 * @note  初始化顺序: NVS → 配置加载 → GPIO → ADC 任务 → CRSF → 模式检测 →
 * 主循环 主循环负责: BLE 更新 / CRSF 链路同步 / 自动对频 / USB HID 输出 /
 * 菜单加载跟踪
 */
void app_main(void) {
    /* ---- 1. NVS 初始化 ---- */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* ---- 2. 加载持久化配置 ---- */
    load_settings_from_nvs();
    bool saved_half_duplex = false;
    bool has_saved_half_duplex = get_saved_crsf_link_mode(&saved_half_duplex);
    bool boot_half_duplex =
        has_saved_half_duplex
            ? saved_half_duplex
            : (CRSF_LINK_MODE == CRSF_LINK_MODE_UART_HALF_DUPLEX);

    /* ---- 3. GPIO 配置 ---- */
    const gpio_config_t mode_select_config = {
        .pin_bit_mask = (1ULL << RC_SWITCH_SA_PIN) |
                        (1ULL << RC_SWITCH_SD_PIN) |
                        (1ULL << RC_SWITCH_SC_PIN) | (1ULL << RC_SWITCH_SB_PIN),
        .mode = GPIO_MODE_INPUT,
        .intr_type = GPIO_INTR_DISABLE,
        .pull_up_en = true,
        .pull_down_en = false,
    };
    ESP_ERROR_CHECK(gpio_config(&mode_select_config));

    /* ---- 3.5 音频播放器初始化 ---- */
    audio_init(NULL, 0); // 默认 I2S 引脚 + 16kHz

    /* ---- 4. 启动 ADC (摇杆读取) — 需在 boot 检测之前 ---- */
    xTaskCreatePinnedToCore(ADC_TASK, "adc_task", 4096, &joy, 4, NULL, 1);
    vTaskDelay(pdMS_TO_TICKS(50));

    /* ---- 5. LED 初始化 ---- */
    led_init();

    /* ---- 5.5 挂载 TF 卡 (音频/瓦片地图)
     * 放在 LED init 之后是因为 SDMMC_CMD 与 WS2812 共用 GPIO48,
     * SDMMC 通过 IOMUX 连接, 会覆盖 GPIO 矩阵.
     * LED 显示最后设置的颜色不变, 闪烁/呼吸/彩虹效果停用. ---- */
    sdcard_mount();

    /* ---- 5.6 开机提示音 — 放在 SD 卡挂载之后, 确保能找到文件 ---- */
    audio_play(SOUND_HELLO);

    /* ---- 6. 检测开机模式 ---- */
    boot_mode_t mode = detect_boot_mode();
    if (mode == BOOT_MODE_UNKNOWN)
        mode = get_default_mode();
    ESP_LOGI(TAG, "开机模式: %s", boot_mode_name(mode));

    /* ---- 7. USB U盘模式 (MSC) — 右摇杆右选择 ---- */
    if (mode == BOOT_MODE_USB_MSC) {
        ESP_LOGI(TAG, ">> USB MSC 模式");
        led_set_mode(LED_MODE_BLE);
        usb_msc_init();
        while (1) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    /* ---- 8. 根据开机模式选择功能模块 ---- */
    const bool crsf_needed =
        (mode == BOOT_MODE_RF || mode == BOOT_MODE_BLE_FPV ||
         mode == BOOT_MODE_PASSTHROUGH || mode == BOOT_MODE_WIFI);
    const bool use_ble =
        (mode == BOOT_MODE_BLE_FPV || mode == BOOT_MODE_PASSTHROUGH);
    const bool use_ble_nus = (mode == BOOT_MODE_PASSTHROUGH);
    const bool use_usb_host = (mode == BOOT_MODE_PASSTHROUGH);
    const bool crsf_always_sync = (mode == BOOT_MODE_WIFI);

    if (crsf_needed) {
        crsf_config_t crsf_cfg = {
            .uart_port = CRSF_UART_PORT,
            .tx_pin =
                boot_half_duplex ? CRSF_UART_HALF_DUPLEX_PIN : CRSF_UART_TX_PIN,
            .rx_pin =
                boot_half_duplex ? CRSF_UART_HALF_DUPLEX_PIN : CRSF_UART_RX_PIN,
            .half_duplex = boot_half_duplex,
            .invert_signal = CRSF_UART_INVERTED,
            .task_priority = 5,
            .task_core_id = 1,
            .on_device_info_cb = my_crsf_device_info_callback,
        };

        if (boot_half_duplex) {
            ESP_LOGI(TAG, "CRSF 启动链路: single-wire (%s) IO=%d inverted=%d",
                     has_saved_half_duplex ? "from NVS"
                                           : "from compile default",
                     CRSF_UART_HALF_DUPLEX_PIN, CRSF_UART_INVERTED);
        } else {
            ESP_LOGI(
                TAG, "CRSF 启动链路: dual-wire (%s) TX=%d RX=%d inverted=%d",
                has_saved_half_duplex ? "from NVS" : "from compile default",
                CRSF_UART_TX_PIN, CRSF_UART_RX_PIN, CRSF_UART_INVERTED);
        }
        crsf_init(&crsf_cfg);
    }

    if (mode == BOOT_MODE_WIFI) {
        rc_wifi_server_init(&joy);
    }

    if (use_ble_nus) {
        ble_init(&joy, BLE_MODE_NUS);
        if (use_usb_host) {
            usb_host_cdc_init();
        }
        bridge_start(BRIDGE_PATH_USB_CDC);
    } else if (use_ble) {
        // BLE HID 初始化
        ble_init(&joy, BLE_MODE_HID);
    }

    if (mode == BOOT_MODE_USB_XBOX) {
        usb_init_mode(SIM_MODE_XBOX);
    } else if (mode == BOOT_MODE_USB_FPV) {
        usb_init_mode(SIM_MODE_DEFAULT);
    }

    /* LED 反映开机模式 */
    switch (mode) {
    case BOOT_MODE_BLE_FPV:
        led_set_mode(LED_MODE_BLE);
        break;
    case BOOT_MODE_WIFI:
        led_set_mode(LED_MODE_WIFI);
        break;
    case BOOT_MODE_USB_XBOX:
        led_set_mode(LED_MODE_USB_XBOX);
        audio_play(SOUND_XBOXMOD);
        break;
    case BOOT_MODE_USB_FPV:
        led_set_mode(LED_MODE_USB_HID);
        audio_play(SOUND_FPVMOD);
        break;
    case BOOT_MODE_PASSTHROUGH:
        led_set_mode(LED_MODE_BLE);
        break;
    case BOOT_MODE_RF:
    default:
        led_set_mode(LED_MODE_CRSF_RF);
        // 射频模式启动时即检测安全条件，不等链路上线
        // throttle: 校准最低值允许 ±6% 噪声容差
        if (joy.throttle > 1060) {
            ESP_LOGI(TAG, "油门不在最低，播放警告");
            audio_play_wait(SOUND_THRALERT, 5000);
        }
        // SB (2段拨码, aux2/CH6): 不在关位 → 警告
        if (joy.aux2 > 1500) {
            ESP_LOGI(TAG, "解锁开关不在初始位置，播放警告");
            audio_play_wait(SOUND_SWALERT, 5000);
        }
        // SC (3段拨码, aux3/CH7): 不在下位(1000) → 警告
        if (joy.aux3 != 1000) {
            ESP_LOGI(TAG, "三段开关不在初始位置，播放警告");
            audio_play_wait(SOUND_SWALERT, 5000);
        }
        break;
    }

    /* ---- 8. 主循环 ---- */
    struct auto_bind_ctx bind = {0};

    bool prev_linked = false;
    bool prev_sb_state = false; // SB 解锁开关上一个状态 (false=锁)
    bool bind_pending = false;  // 正在连接中
    uint32_t bind_start_ms = 0; // 开始连接的时间戳
    uint32_t last_rssi_warn_ms = 0;

    bool printed_full_snapshot = false;
    uint8_t last_loaded_params = 0;

    while (1) {
        uint32_t now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);

        /* --- BLE 输入更新 --- */
        if (use_ble)
            ble_update_input(&joy);

        /* --- LED 轮询 (处理 BIND 闪烁) --- */
        led_poll();

        /* --- CRSF 链路同步 --- */
        if (crsf_needed) {
            crsf_state_t *state = crsf_get_state();

            const bool linked = state->is_linked;

            /* ---- SD 连接按钮（仅在非 WiFi 模式下） ---- */
            if (!crsf_always_sync) {
                if (!linked && !bind_pending) {
                    // 未连接 + 未在绑定中 → SD 作为连接按钮
                    if (gpio_get_level(RC_SWITCH_SD_PIN) == 0) {
                        ESP_LOGI(TAG, ">> SD 按下，开始连接接收机");
                        audio_play(SOUND_BINDING);
                        uint8_t bind_param = find_bind_param_id(state);
                        if (bind_param == 0)
                            bind_param = FALLBACK_BIND_PARAM_ID;
                        crsf_write_menu_value(bind_param, 1);
                        bind_pending = true;
                        bind_start_ms = now_ms;
                        led_set_mode(LED_MODE_BIND);
                    }
                }

                if (bind_pending) {
                    if (linked) {
                        // 连接成功 — telemok 会自然发出"回传恢复"
                        bind_pending = false;
                        led_set_mode(LED_MODE_CRSF_RF);
                        ESP_LOGI(TAG, "连接成功");
                    } else if (now_ms - bind_start_ms >= BIND_TIMEOUT_MS) {
                        // 超时
                        bind_pending = false;
                        audio_play(SOUND_BINDFAIL);
                        led_set_mode(LED_MODE_CRSF_RF);
                        ESP_LOGW(TAG, "连接超时（%ums）", BIND_TIMEOUT_MS);
                    }
                }
            }

            /* ---- 回传状态语音（跟踪 is_linked 变化） ---- */
            if (linked != prev_linked) {
                prev_linked = linked;
                if (linked) {
                    audio_play(SOUND_TELEMOK);
                } else if (!bind_pending) {
                    // 不是正在尝试连接断开 → 播回传丢失
                    audio_play(SOUND_TELEMKO);
                }
            }

            /* ---- SB 解锁开关（2段拨码）边沿检测 ---- */
            bool sb_now = (joy.aux2 > 1500); // SB → CH6 → aux2
            if (sb_now != prev_sb_state) {
                prev_sb_state = sb_now;
                if (sb_now) {
                    audio_play(SOUND_ARMED); // 打开 → 已解锁
                } else {
                    audio_play(SOUND_LOCKED); // 关闭 → 已锁定
                }
            }

            /* ---- RSSI 弱信号语音 ---- */
            if (linked) {
                if (state->rssi > 0 && state->rssi < RSSI_WARN_RED) {
                    if (now_ms - last_rssi_warn_ms >= RSSI_RED_INTERVAL_MS) {
                        last_rssi_warn_ms = now_ms;
                        audio_play(SOUND_RSSI_RED);
                    }
                } else if (state->rssi < RSSI_WARN_ORG) {
                    if (now_ms - last_rssi_warn_ms >= RSSI_ORG_INTERVAL_MS) {
                        last_rssi_warn_ms = now_ms;
                        audio_play(SOUND_RSSI_ORG);
                    }
                }
            }

            /* ---- 通道发送 ---- */
            if (crsf_always_sync || linked)
                sync_joy_to_crsf(&joy);

            /* ---- CRSF 菜单加载进度跟踪 ---- */
            if (state->loaded_params != last_loaded_params) {
                last_loaded_params = state->loaded_params;
                if (!printed_full_snapshot && state->total_params > 0 &&
                    state->loaded_params < state->total_params) {
                    ESP_LOGI(TAG, "菜单加载中: %u / %u", state->loaded_params,
                             state->total_params);
                }
            }

            if (state->is_ready && state->total_params > 0 &&
                state->loaded_params == state->total_params &&
                !printed_full_snapshot) {
                print_crsf_state_snapshot(state);
                printed_full_snapshot = true;
            }

            if (state->loaded_params == 0)
                printed_full_snapshot = false;

            /* --- 回传数据状态打印（5秒间隔） --- */
#if 0
            {
                static uint32_t s_last_telem_print = 0;
                if (now_ms - s_last_telem_print >= 5000) {
                    s_last_telem_print = now_ms;
                    const crsf_telemetry_t *t = &state->telemetry;
                    if (t->last_update_ms > 0) {
                        const uint16_t alt_m = (t->gps.altitude > 1000)
                                                   ? (t->gps.altitude - 1000) : 0;
                        const double yaw_deg = (double)t->attitude.yaw * 180.0 / 31415.9;
                        const double yaw_deg_norm = (yaw_deg < 0) ? yaw_deg + 360.0 : yaw_deg;
                        ESP_LOGI(TAG,
                                 "\n"
                                 "╔═══════════════════ CRSF 遥测 ═══════════════════\n"
                                 "║ ⚡ 电池  %.1fV / %.1fA  %umAh  %u%%\n"
                                 "║ 🛰 GPS    %.6f, %.6f\n"
                                 "║          %um  %ucm/s  %u°  %u颗\n"
                                 "║ 📐 姿态  Pitch %+.1f°  Roll %+.1f°  Yaw %.1f°\n"
                                 "║ 📊 气压  %.0fm  %+.1fm/s \n"
                                 "║ 🏷 模式  %s \n"
                                 "╚══════════════════════════════════════════════════\n",
                                 (double)t->battery.voltage / 10.0,
                                 (double)t->battery.current / 10.0, t->battery.capacity,
                                 t->battery.remaining,
                                 (double)t->gps.latitude / 1e7,
                                 (double)t->gps.longitude / 1e7,
                                 alt_m, t->gps.speed,
                                 t->gps.heading / 10,
                                 t->gps.sats,
                                 (double)t->attitude.pitch * 180.0 / 31415.9,
                                 (double)t->attitude.roll * 180.0 / 31415.9,
                                 yaw_deg_norm,
                                 (double)t->vario.altitude / 100.0,
                                 (double)t->vario.vSpeed / 100.0,
                                 t->flight_mode);
                    }
                }
            }
#endif

            crsf_send_device_ping();
        }

        /* --- USB HID 数据发送 --- */
        if (tud_mounted())
            app_send_fpv_data(&joy);

        vTaskDelay(1);
    }
}
