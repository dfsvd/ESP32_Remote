
/* =========================================================================
 * FPV 遥控器 — 主入口
 *
 * 功能概要：
 *   - 根据 GPIO 拨码开关决定运行模式 (USB / BLE / WiFi / 纯射频)
 *   - 驱动 CRSF 协议栈与高频头通信
 *   - 自动对频状态机
 *   - 摇杆→CRSF 通道同步
 *   - USB HID 手柄输出 (含 Xbox 模式)
 * ========================================================================= */

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "rc_ble.h"
#include "rc_crsf.h"
#include "rc_read.h"
#include "rc_usb.h"
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

#define APP_BUTTON                      (GPIO_NUM_0)
#define CRSF_LINK_MODE_UART_FULL_DUPLEX 0
#define CRSF_LINK_MODE_UART_HALF_DUPLEX 1
#define CRSF_LINK_MODE                  CRSF_LINK_MODE_UART_FULL_DUPLEX
#define CRSF_UART_PORT                  UART_NUM_1
#define CRSF_UART_TX_PIN                GPIO_NUM_17
#define CRSF_UART_RX_PIN                GPIO_NUM_16
#define CRSF_UART_HALF_DUPLEX_PIN       GPIO_NUM_17
#define CRSF_UART_INVERTED              0
#define CRSF_PARAM_TYPE_COMMAND         0x0D
#define FALLBACK_BIND_PARAM_ID          18
#define AUTO_BIND_INTERVAL_MS           2000U
#define AUTO_BIND_LINK_STABLE_MS        1200U

/* =========================================================================
 * 全局状态
 * ========================================================================= */

/**
 * @brief 全局摇杆状态，供 USB / BLE / WiFi / CRSF 模块共享
 * @note  初始值 1500(中位) / 1000(低位)，防止未启用的通道发送 0 导致
 *         Windows 丢弃整个 HID 报文
 */
static fpv_joystick_report_t joy = {
    .roll     = 1500,
    .pitch    = 1500,
    .throttle = 1000,
    .yaw      = 1500,
    .aux1     = 1000,
    .aux2     = 1000,
    .aux3     = 1000,
    .aux4     = 1000,
    .sw1      = 1000,
    .sw2      = 1000,
    .sw3      = 1000,
    .sw4      = 1000,
    .sw5      = 1000,
    .sw6      = 1000,
    .sw7      = 1000,
    .sw8      = 1000,
};

/**
 * @brief 自动对频状态机上下文
 */
struct auto_bind_ctx
{
    bool     active;           /**< 是否处于自动对频模式                   */
    bool     complete_logged;  /**< 是否已输出"对频成功"日志                */
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
static void sanitize_ascii(char *dst, size_t dst_size, const char *src)
{
    size_t out = 0;

    if (!dst || dst_size == 0)
        return;
    dst[0] = '\0';
    if (!src)
        return;

    for (size_t i = 0; src[i] != '\0' && out + 1 < dst_size; ++i)
    {
        unsigned char c = (unsigned char)src[i];
        if (c == 0xC0)
        {
            int written = snprintf(dst + out, dst_size - out, ":Lo");
            if (written <= 0 || (size_t)written >= dst_size - out)
                break;
            out += (size_t)written;
            continue;
        }
        if (c == 0xC1)
        {
            int written = snprintf(dst + out, dst_size - out, ":Hi");
            if (written <= 0 || (size_t)written >= dst_size - out)
                break;
            out += (size_t)written;
            continue;
        }
        if (c >= 32 && c <= 126)
        {
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
static const char *crsf_menu_type_name(uint8_t type)
{
    switch (type)
    {
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
static bool contains_case_insensitive(const char *text, const char *token)
{
    if (!text || !token || token[0] == '\0')
        return false;

    size_t token_len = strlen(token);
    size_t text_len  = strlen(text);
    if (token_len > text_len)
        return false;

    for (size_t i = 0; i + token_len <= text_len; ++i)
    {
        bool matched = true;
        for (size_t j = 0; j < token_len; ++j)
        {
            char a = (char)tolower((unsigned char)text[i + j]);
            char b = (char)tolower((unsigned char)token[j]);
            if (a != b)
            {
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
static void print_crsf_state_snapshot(const crsf_state_t *db)
{
    if (!db)
        return;

    ESP_LOGW(TAG, "================ 📊 CRSF 全局快照 ================");
    ESP_LOGI(TAG, "[状态] 握手:%s | 飞机:%s | RSSI:-%d | LQ:%u | SNR:%d",
             db->is_ready ? "是" : "否", db->is_linked ? "是" : "否", db->rssi, db->lq, db->snr);
    ESP_LOGI(TAG, "[加载] %d / %d (有效/总计)", db->loaded_params, db->total_params);
    ESP_LOGW(TAG, "---------------- 📂 菜单列表 ----------------");

    for (int i = 0; i < CRSF_MAX_MENU_ITEMS; i++)
    {
        if (db->menu[i].id == 0 || !db->menu[i].is_valid)
            continue;

        const char *n         = db->menu[i].name;
        const char *extra     = db->menu[i].options[0] ? db->menu[i].options : "-";
        const char *type_name = crsf_menu_type_name(db->menu[i].type);
        char        safe_name[160];
        char        safe_extra[768];

        sanitize_ascii(safe_name, sizeof(safe_name), n);
        sanitize_ascii(safe_extra, sizeof(safe_extra), extra);

        if (db->menu[i].type == 0x09)
        {
            ESP_LOGI(TAG, " [%02d] <%s> 值:%-3d | 父:%-2d | 名称:%-18s | 选项:%s", db->menu[i].id,
                     type_name, db->menu[i].value, db->menu[i].parent_id, safe_name, safe_extra);
        }
        else if (db->menu[i].type == 0x0C || db->menu[i].type == 0x0A)
        {
            ESP_LOGI(TAG, " [%02d] <%s>        | 父:%-2d | 名称:%-18s | 内容:%s", db->menu[i].id,
                     type_name, db->menu[i].parent_id, safe_name, safe_extra);
        }
        else if (db->menu[i].type == 0x0D)
        {
            ESP_LOGI(TAG, " [%02d] <%s> 状态:%-3d | 父:%-2d | 名称:%-18s | 信息:%s", db->menu[i].id,
                     type_name, db->menu[i].value, db->menu[i].parent_id, safe_name, safe_extra);
        }
        else
        {
            ESP_LOGI(TAG, " [%02d] <%s>        | 父:%-2d | 名称:%-18s", db->menu[i].id, type_name,
                     db->menu[i].parent_id, safe_name);
        }
    }

    ESP_LOGW(TAG, "================================================");
}

/**
 * @brief 将摇杆报告的 16 通道数据同步到 CRSF 协议栈
 * @note  通道映射: CH1=roll, CH2=pitch, CH3=throttle, CH4=yaw,
 *                   CH5-8=aux1-4, CH9-16=sw1-8
 * @param report 摇杆数据报告指针，为 NULL 时直接返回
 */
static void sync_joy_to_crsf(const fpv_joystick_report_t *report)
{
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
static uint8_t find_bind_param_id(const crsf_state_t *state)
{
    if (!state)
        return 0;

    for (int i = 0; i < CRSF_MAX_MENU_ITEMS; ++i)
    {
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
 * @brief 根据 GPIO 拨码开关位置检测开机模式并初始化对应通信模块
 * @note  模式判定逻辑:
 *        CH1=高 → 纯射频模式 (不开启 USB/BLE/WiFi)
 *        CH1=低 + 三段开关上=低 → BLE 模式
 *        CH1=低 + 三段开关下=低 → WiFi 模式
 *        CH1=低 + 三段开关中位   → USB 模式
 *        CH3=低                 → 开启自动对频
 * @param host_mode_selected 输出: 是否进入主机模式 (USB/BLE/WiFi)
 * @param ble_mode           输出: 1=BLE 模式, 0=非 BLE
 * @param bind_mode_active   输出: 是否进入自动对频模式
 */
static void detect_boot_mode(bool *host_mode_selected, uint8_t *ble_mode, bool *bind_mode_active)
{
    *host_mode_selected = false;
    *ble_mode           = 0;
    *bind_mode_active   = false;

    bool ch1_low  = (gpio_get_level(RC_SWITCH_2POS_PIN_CH1) == 0);
    bool ch3_low  = (gpio_get_level(RC_SWITCH_2POS_PIN_CH3) == 0);
    bool up_low   = (gpio_get_level(RC_SWITCH_3POS_UP_PIN) == 0);
    bool down_low = (gpio_get_level(RC_SWITCH_3POS_DOWN_PIN) == 0);

    if (ch1_low)
    {
        *host_mode_selected = true;
        if (up_low)
        {
            ble_init(&joy);
            *ble_mode = 1;
            ESP_LOGI(TAG, "开机模式: BLE (CH1低 + 3段上低)");
        }
        else if (down_low)
        {
            rc_wifi_server_init(&joy);
            ESP_LOGI(TAG, "开机模式: WiFi (CH1低 + 3段下低)");
        }
        else
        {
            usb_init();
            ESP_LOGI(TAG, "开机模式: USB (CH1低 + 3段中位)");
        }
    }
    else
    {
        ESP_LOGI(TAG, "开机模式: 纯射频模式 (CH1高)");
    }

    if (ch3_low)
    {
        *bind_mode_active = true;
        ESP_LOGI(TAG, "检测到 CH3 低电平，进入开机自动对频模式");
    }
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
static void poll_auto_bind(const crsf_state_t *state, uint32_t now_ms, struct auto_bind_ctx *ctx)
{
    if (!state || !ctx || !ctx->active)
        return;

    /* --- 状态 1: 接收机已连接，等待链路稳定 --- */
    if (state->is_linked)
    {
        if (ctx->link_up_since_ms == 0)
        {
            ctx->link_up_since_ms = now_ms;
            ESP_LOGI(TAG, "检测到链路上线，开始验证对频稳定性...");
        }
        if ((now_ms - ctx->link_up_since_ms) >= AUTO_BIND_LINK_STABLE_MS)
        {
            ctx->active = false;
            if (!ctx->complete_logged)
            {
                ESP_LOGI(TAG, "对频成功(链路稳定 %ums)，退出自动对频模式",
                         AUTO_BIND_LINK_STABLE_MS);
                ctx->complete_logged = true;
            }
        }
    }
    /* --- 状态 2: 高频头就绪，周期性发送 bind 命令 --- */
    else if (state->is_ready && state->total_params > 0 &&
             state->loaded_params == state->total_params)
    {
        ctx->link_up_since_ms = 0;
        if (now_ms - ctx->last_try_ms >= AUTO_BIND_INTERVAL_MS)
        {
            uint8_t bind_param_id = find_bind_param_id(state);
            if (bind_param_id == 0)
                bind_param_id = FALLBACK_BIND_PARAM_ID;

            crsf_write_menu_value(bind_param_id, 1);
            ctx->last_try_ms = now_ms;
            ESP_LOGI(TAG, "自动对频发包: PARAM_WRITE id=%u value=1 (间隔=%ums)", bind_param_id,
                     AUTO_BIND_INTERVAL_MS);
        }
    }
    /* --- 状态 3: 等待高频头就绪 --- */
    else if ((now_ms - ctx->last_wait_log_ms) >= 2000U)
    {
        ctx->last_wait_log_ms = now_ms;
        ESP_LOGI(TAG, "自动对频等待中: ready=%d menu=%u/%u linked=%d", state->is_ready ? 1 : 0,
                 state->loaded_params, state->total_params, state->is_linked ? 1 : 0);
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
void my_crsf_device_info_callback(const char *name)
{
    ESP_LOGI(TAG, "【应用层收到高频头信息】: %s", name);
}

/* =========================================================================
 * 主入口
 * ========================================================================= */

/**
 * @brief ESP32 应用程序主入口
 * @note  初始化顺序: NVS → 配置加载 → GPIO → ADC 任务 → CRSF → 模式检测 → 主循环
 *        主循环负责: BLE 更新 / CRSF 链路同步 / 自动对频 / USB HID 输出 / 菜单加载跟踪
 */
void app_main(void)
{
    /* ---- 1. NVS 初始化 ---- */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* ---- 2. 加载持久化配置 ---- */
    load_settings_from_nvs();
    bool saved_half_duplex     = false;
    bool has_saved_half_duplex = get_saved_crsf_link_mode(&saved_half_duplex);
    bool boot_half_duplex      = has_saved_half_duplex
                                     ? saved_half_duplex
                                     : (CRSF_LINK_MODE == CRSF_LINK_MODE_UART_HALF_DUPLEX);

    /* ---- 3. GPIO 配置 ---- */
    const gpio_config_t boot_button_config = {
        .pin_bit_mask = BIT64(APP_BUTTON),
        .mode         = GPIO_MODE_INPUT,
        .intr_type    = GPIO_INTR_DISABLE,
        .pull_up_en   = true,
        .pull_down_en = false,
    };
    const gpio_config_t mode_select_config = {
        .pin_bit_mask = (1ULL << RC_SWITCH_2POS_PIN_CH1) | (1ULL << RC_SWITCH_2POS_PIN_CH3) |
                        (1ULL << RC_SWITCH_3POS_UP_PIN) | (1ULL << RC_SWITCH_3POS_DOWN_PIN),
        .mode         = GPIO_MODE_INPUT,
        .intr_type    = GPIO_INTR_DISABLE,
        .pull_up_en   = true,
        .pull_down_en = false,
    };
    ESP_ERROR_CHECK(gpio_config(&boot_button_config));
    ESP_ERROR_CHECK(gpio_config(&mode_select_config));

    /* ---- 4. CRSF 初始化 ---- */
    crsf_config_t crsf_cfg = {
        .uart_port         = CRSF_UART_PORT,
        .tx_pin            = boot_half_duplex ? CRSF_UART_HALF_DUPLEX_PIN : CRSF_UART_TX_PIN,
        .rx_pin            = boot_half_duplex ? CRSF_UART_HALF_DUPLEX_PIN : CRSF_UART_RX_PIN,
        .half_duplex       = boot_half_duplex,
        .invert_signal     = CRSF_UART_INVERTED,
        .task_priority     = 5,
        .task_core_id      = 1,
        .on_device_info_cb = my_crsf_device_info_callback,
    };

    if (boot_half_duplex)
    {
        ESP_LOGI(TAG, "CRSF 启动链路: single-wire (%s) IO=%d inverted=%d",
                 has_saved_half_duplex ? "from NVS" : "from compile default",
                 CRSF_UART_HALF_DUPLEX_PIN, CRSF_UART_INVERTED);
    }
    else
    {
        ESP_LOGI(TAG, "CRSF 启动链路: dual-wire (%s) TX=%d RX=%d inverted=%d",
                 has_saved_half_duplex ? "from NVS" : "from compile default", CRSF_UART_TX_PIN,
                 CRSF_UART_RX_PIN, CRSF_UART_INVERTED);
    }

    /* ---- 5. 启动 ADC 读取任务与 CRSF 协议栈 ---- */
    xTaskCreatePinnedToCore(ADC_TASK, "adc_task", 4096, &joy, 4, NULL, 1);
    crsf_init(&crsf_cfg);
    vTaskDelay(pdMS_TO_TICKS(50));

    /* ---- 6. 开机模式检测 ---- */
    bool    host_mode_selected = false;
    uint8_t ble_mode           = 0;
    bool    bind_mode_active   = false;
    // 调试: 临时强制 WiFi 模式，方便测试 web 修改
    // 恢复 GPIO 选择时注释下面三行，取消上一行 detect_boot_mode 的注释即可
    host_mode_selected = true;
    rc_wifi_server_init(&joy);
    ESP_LOGI(TAG, "开机模式: WiFi (调试强制模式)");
    //detect_boot_mode(&host_mode_selected, &ble_mode, &bind_mode_active);

    /* ---- 7. 主循环 ---- */
    struct auto_bind_ctx bind = {0};
    bind.active               = bind_mode_active;

    bool    printed_full_snapshot = false;
    bool    waiting_link_logged   = false;
    bool    link_ready_logged     = false;
    uint8_t last_loaded_params    = 0;

    while (1)
    {
        crsf_state_t *state  = crsf_get_state();
        uint32_t      now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);

        /* --- BLE 输入更新 --- */
        if (ble_mode)
            ble_update_input(&joy);

        /* --- CRSF 链路同步 --- */
        if (host_mode_selected)
        {
            sync_joy_to_crsf(&joy);
        }
        else if (state->is_linked)
        {
            if (!link_ready_logged)
            {
                ESP_LOGI(TAG, "高频头已连接接收机，开始发送全部通道数据");
                link_ready_logged = true;
            }
            waiting_link_logged = false;
            sync_joy_to_crsf(&joy);
        }
        else if (!waiting_link_logged)
        {
            ESP_LOGI(TAG, "等待高频头连接接收机...");
            waiting_link_logged = true;
            link_ready_logged   = false;
        }

        /* --- 自动对频 --- */
        poll_auto_bind(state, now_ms, &bind);

        /* --- USB HID 数据发送 --- */
        if (tud_mounted())
            app_send_fpv_data(&joy);

        /* --- CRSF 菜单加载进度跟踪 --- */
        if (state->loaded_params != last_loaded_params)
        {
            last_loaded_params = state->loaded_params;
            if (!printed_full_snapshot && state->total_params > 0 &&
                state->loaded_params < state->total_params)
            {
                ESP_LOGI(TAG, "菜单加载中: %u / %u", state->loaded_params, state->total_params);
            }
        }

        if (state->is_ready && state->total_params > 0 &&
            state->loaded_params == state->total_params && !printed_full_snapshot)
        {
            print_crsf_state_snapshot(state);
            printed_full_snapshot = true;
        }

        if (state->loaded_params == 0)
            printed_full_snapshot = false;

        crsf_send_device_ping();
        vTaskDelay(1);
    }
}
