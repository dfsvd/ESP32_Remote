
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "rc_usb.h"
#include "tinyusb.h"
#include "rc_read.h"
#include "rc_wf.h"
#include "rc_crsf.h"
#include "rc_ble.h"
#include "nvs_flash.h"

static const char *TAG = "FPV_RC";
#define APP_BUTTON (GPIO_NUM_0) // Use BOOT signal by default
#define CRSF_LINK_MODE_UART_FULL_DUPLEX 0
#define CRSF_LINK_MODE_UART_HALF_DUPLEX 1
#define CRSF_LINK_MODE CRSF_LINK_MODE_UART_FULL_DUPLEX
#define CRSF_UART_PORT   UART_NUM_1
#define CRSF_UART_TX_PIN GPIO_NUM_17
#define CRSF_UART_RX_PIN GPIO_NUM_16
#define CRSF_UART_HALF_DUPLEX_PIN GPIO_NUM_17
#define CRSF_UART_INVERTED 0
#define CRSF_PARAM_TYPE_COMMAND 0x0D
#define FALLBACK_BIND_PARAM_ID 18
#define AUTO_BIND_INTERVAL_MS 2000U
#define AUTO_BIND_LINK_STABLE_MS 1200U
// 赋初始合法值，防止未启用的通道发送 0 导致 Windows 丢弃整个 HID 报文
static fpv_joystick_report_t joy = {
    .roll = 1500, .pitch = 1500, .throttle = 1000, .yaw = 1500,
    .aux1 = 1000, .aux2 = 1000, .aux3 = 1000, .aux4 = 1000,
    // 关键点：提前给还没有写逻辑的开关通道垫上 1000
    .sw1 = 1000, .sw2 = 1000, .sw3 = 1000, .sw4 = 1000,
    .sw5 = 1000, .sw6 = 1000, .sw7 = 1000, .sw8 = 1000
};

static void sanitize_ascii(char *dst, size_t dst_size, const char *src) {
    size_t out = 0;

    if (!dst || dst_size == 0) return;
    dst[0] = '\0';
    if (!src) return;

    for (size_t i = 0; src[i] != '\0' && out + 1 < dst_size; ++i) {
        unsigned char c = (unsigned char)src[i];
        if (c == 0xC0) {
            int written = snprintf(dst + out, dst_size - out, ":Lo");
            if (written <= 0 || (size_t)written >= dst_size - out) break;
            out += (size_t)written;
            continue;
        }
        if (c == 0xC1) {
            int written = snprintf(dst + out, dst_size - out, ":Hi");
            if (written <= 0 || (size_t)written >= dst_size - out) break;
            out += (size_t)written;
            continue;
        }
        if (c >= 32 && c <= 126) {
            dst[out++] = (char)c;
            continue;
        }

        int written = snprintf(dst + out, dst_size - out, "\\x%02X", c);
        if (written <= 0 || (size_t)written >= dst_size - out) break;
        out += (size_t)written;
    }

    dst[out] = '\0';
}

static const char *crsf_menu_type_name(uint8_t type) {
    switch (type) {
        case 0x09: return "选项";
        case 0x0A: return "文本";
        case 0x0B: return "目录";
        case 0x0C: return "信息";
        case 0x0D: return "命令";
        default:   return "常规";
    }
}

static void print_crsf_state_snapshot(const crsf_state_t *db) {
    if (!db) return;

    ESP_LOGW(TAG, "================ 📊 CRSF 全局快照 ================");
    ESP_LOGI(TAG, "[状态] 握手:%s | 飞机:%s | RSSI:-%d | LQ:%u | SNR:%d",
             db->is_ready ? "是" : "否",
             db->is_linked ? "是" : "否",
             db->rssi,
             db->lq,
             db->snr);
    ESP_LOGI(TAG, "[加载] %d / %d (有效/总计)", db->loaded_params, db->total_params);
    ESP_LOGW(TAG, "---------------- 📂 菜单列表 ----------------");

    for (int i = 0; i < CRSF_MAX_MENU_ITEMS; i++) {
        if (db->menu[i].id == 0 || !db->menu[i].is_valid) continue;

        const char *n = db->menu[i].name;
        const char *extra = db->menu[i].options[0] ? db->menu[i].options : "-";
        const char *type_name = crsf_menu_type_name(db->menu[i].type);
        char safe_name[160];
        char safe_extra[768];

        sanitize_ascii(safe_name, sizeof(safe_name), n);
        sanitize_ascii(safe_extra, sizeof(safe_extra), extra);

        if (db->menu[i].type == 0x09) {
            ESP_LOGI(TAG, " [%02d] <%s> 值:%-3d | 父:%-2d | 名称:%-18s | 选项:%s",
                     db->menu[i].id, type_name, db->menu[i].value,
                     db->menu[i].parent_id, safe_name, safe_extra);
        } else if (db->menu[i].type == 0x0C || db->menu[i].type == 0x0A) {
            ESP_LOGI(TAG, " [%02d] <%s>        | 父:%-2d | 名称:%-18s | 内容:%s",
                     db->menu[i].id, type_name, db->menu[i].parent_id,
                     safe_name, safe_extra);
        } else if (db->menu[i].type == 0x0D) {
            ESP_LOGI(TAG, " [%02d] <%s> 状态:%-3d | 父:%-2d | 名称:%-18s | 信息:%s",
                     db->menu[i].id, type_name, db->menu[i].value,
                     db->menu[i].parent_id, safe_name, safe_extra);
        } else {
            ESP_LOGI(TAG, " [%02d] <%s>        | 父:%-2d | 名称:%-18s",
                     db->menu[i].id, type_name, db->menu[i].parent_id, safe_name);
        }
    }

    ESP_LOGW(TAG, "================================================");
}

static void sync_joy_to_crsf(const fpv_joystick_report_t *report) {
    if (!report) return;

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

static bool contains_case_insensitive(const char *text, const char *token)
{
    if (!text || !token || token[0] == '\0') {
        return false;
    }

    size_t token_len = strlen(token);
    size_t text_len = strlen(text);
    if (token_len > text_len) {
        return false;
    }

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
        if (matched) {
            return true;
        }
    }

    return false;
}

static uint8_t find_bind_param_id(const crsf_state_t *state)
{
    if (!state) {
        return 0;
    }

    for (int i = 0; i < CRSF_MAX_MENU_ITEMS; ++i) {
        const crsf_menu_item_t *item = &state->menu[i];
        if (!item->is_valid || item->type != CRSF_PARAM_TYPE_COMMAND) {
            continue;
        }
        if (contains_case_insensitive(item->name, "bind")) {
            return item->id;
        }
    }

    return 0;
}

void my_crsf_device_info_callback(const char *name) {
    // 你终于在 main.c 里拿到高频头的名字了！
    // 如果你有屏幕，你可以直接在这里调用 lvgl 的接口把 name 显示在屏幕上：
    // lv_label_set_text(my_label, name);
    
    ESP_LOGI(TAG, "【应用层收到高频头信息】: %s", name);
}

void app_main(void)
{
    uint8_t ble_mode = 0;
    uint8_t last_loaded_params = 0;
    bool printed_full_snapshot = false;
    bool host_mode_selected = false;
    bool bind_mode_active = false;
    bool bind_complete_logged = false;
    bool waiting_link_logged = false;
    bool link_ready_logged = false;
    uint32_t last_bind_try_ms = 0;
    uint32_t bind_link_up_since_ms = 0;
    uint32_t last_bind_wait_log_ms = 0;
    bool saved_half_duplex = false;
    bool has_saved_half_duplex = false;
    bool boot_half_duplex = false;
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
  
    load_settings_from_nvs();
    has_saved_half_duplex = get_saved_crsf_link_mode(&saved_half_duplex);
    boot_half_duplex = has_saved_half_duplex
        ? saved_half_duplex
        : (CRSF_LINK_MODE == CRSF_LINK_MODE_UART_HALF_DUPLEX);
    // Initialize button that will trigger HID reports
    const gpio_config_t boot_button_config = {
        .pin_bit_mask = BIT64(APP_BUTTON),
        .mode = GPIO_MODE_INPUT,
        .intr_type = GPIO_INTR_DISABLE,
        .pull_up_en = true,
        .pull_down_en = false,
    };
    const gpio_config_t mode_select_config = {
        .pin_bit_mask = (1ULL << RC_SWITCH_2POS_PIN_CH1) |
                        (1ULL << RC_SWITCH_2POS_PIN_CH3) |
                        (1ULL << RC_SWITCH_3POS_UP_PIN) |
                        (1ULL << RC_SWITCH_3POS_DOWN_PIN),
        .mode = GPIO_MODE_INPUT,
        .intr_type = GPIO_INTR_DISABLE,
        .pull_up_en = true,
        .pull_down_en = false,
    };
    crsf_config_t crsf_cfg = {
        .uart_port = CRSF_UART_PORT,
        .tx_pin = boot_half_duplex ? CRSF_UART_HALF_DUPLEX_PIN : CRSF_UART_TX_PIN,
        .rx_pin = boot_half_duplex ? CRSF_UART_HALF_DUPLEX_PIN : CRSF_UART_RX_PIN,
        .half_duplex = boot_half_duplex,
        .invert_signal = CRSF_UART_INVERTED,
        .task_priority = 5,
        .task_core_id = 1,
        // 👇 把刚才写的函数名字塞进去
        .on_device_info_cb = my_crsf_device_info_callback 
    };
    ESP_ERROR_CHECK(gpio_config(&boot_button_config));
    ESP_ERROR_CHECK(gpio_config(&mode_select_config));
    if (boot_half_duplex) {
        ESP_LOGI(
            TAG,
            "CRSF 启动链路: single-wire (%s) IO=%d inverted=%d",
            has_saved_half_duplex ? "from NVS" : "from compile default",
            CRSF_UART_HALF_DUPLEX_PIN,
            CRSF_UART_INVERTED);
    } else {
        ESP_LOGI(
            TAG,
            "CRSF 启动链路: dual-wire (%s) TX=%d RX=%d inverted=%d",
            has_saved_half_duplex ? "from NVS" : "from compile default",
            CRSF_UART_TX_PIN,
            CRSF_UART_RX_PIN,
            CRSF_UART_INVERTED);
    }
    xTaskCreatePinnedToCore( ADC_TASK, "adc_task", 4096, &joy,  4, NULL, 1 );
    crsf_init(&crsf_cfg);

    vTaskDelay(pdMS_TO_TICKS(50));

    bool ch1_low = (gpio_get_level(RC_SWITCH_2POS_PIN_CH1) == 0);
    bool ch3_low = (gpio_get_level(RC_SWITCH_2POS_PIN_CH3) == 0);
    bool up_low = (gpio_get_level(RC_SWITCH_3POS_UP_PIN) == 0);
    bool down_low = (gpio_get_level(RC_SWITCH_3POS_DOWN_PIN) == 0);

    if (ch1_low) {
        host_mode_selected = true;
        if (up_low) {
            ble_init(&joy);
            ble_mode = 1;
            ESP_LOGI(TAG, "开机模式: BLE (CH1低 + 3段上低)");
        } else if (down_low) {
            rc_wifi_server_init(&joy);
            ESP_LOGI(TAG, "开机模式: WiFi (CH1低 + 3段下低)");
        } else {
            usb_init();
            ESP_LOGI(TAG, "开机模式: USB (CH1低 + 3段中位)");
        }
    } else {
        ESP_LOGI(TAG, "开机模式: 纯射频模式 (CH1高)");
    }

    if (ch3_low) {
        bind_mode_active = true;
        ESP_LOGI(TAG, "检测到 CH3 低电平，进入开机自动对频模式");
    }

    while (1)
    {
        crsf_state_t *state = crsf_get_state();
        uint32_t now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);

        if (ble_mode) 
        {
            ble_update_input(&joy);
        }

        if (host_mode_selected) {
            sync_joy_to_crsf(&joy);
        } else if (state->is_linked) {
            if (!link_ready_logged) {
                ESP_LOGI(TAG, "高频头已连接接收机，开始发送全部通道数据");
                link_ready_logged = true;
            }
            waiting_link_logged = false;
            sync_joy_to_crsf(&joy);
        } else if (!waiting_link_logged) {
            ESP_LOGI(TAG, "等待高频头连接接收机...");
            waiting_link_logged = true;
            link_ready_logged = false;
        }

        if (bind_mode_active) {
            if (state->is_linked) {
                if (bind_link_up_since_ms == 0) {
                    bind_link_up_since_ms = now_ms;
                    ESP_LOGI(TAG, "检测到链路上线，开始验证对频稳定性...");
                }
                if ((now_ms - bind_link_up_since_ms) >= AUTO_BIND_LINK_STABLE_MS) {
                    bind_mode_active = false;
                    if (!bind_complete_logged) {
                        ESP_LOGI(TAG, "对频成功(链路稳定 %ums)，退出自动对频模式", AUTO_BIND_LINK_STABLE_MS);
                        bind_complete_logged = true;
                    }
                }
            } else if (state->is_ready && state->total_params > 0 && state->loaded_params == state->total_params) {
                bind_link_up_since_ms = 0;
                if (now_ms - last_bind_try_ms >= AUTO_BIND_INTERVAL_MS) {
                    uint8_t bind_param_id = find_bind_param_id(state);
                    if (bind_param_id == 0) {
                        bind_param_id = FALLBACK_BIND_PARAM_ID;
                    }
                    crsf_write_menu_value(bind_param_id, 1);
                    last_bind_try_ms = now_ms;
                    ESP_LOGI(TAG, "自动对频发包: PARAM_WRITE id=%u value=1 (间隔=%ums)", bind_param_id, AUTO_BIND_INTERVAL_MS);
                }
            } else if ((now_ms - last_bind_wait_log_ms) >= 2000U) {
                last_bind_wait_log_ms = now_ms;
                ESP_LOGI(TAG, "自动对频等待中: ready=%d menu=%u/%u linked=%d",
                         state->is_ready ? 1 : 0,
                         state->loaded_params,
                         state->total_params,
                         state->is_linked ? 1 : 0);
                bind_link_up_since_ms = 0;
            }
        }

        if (tud_mounted())
        {
            app_send_fpv_data(&joy);
        }

        if (state->loaded_params != last_loaded_params) {
            last_loaded_params = state->loaded_params;
            if (!printed_full_snapshot && state->total_params > 0 &&
                state->loaded_params < state->total_params) {
                ESP_LOGI(TAG, "菜单加载中: %u / %u", state->loaded_params, state->total_params);
            }
        }

        if (state->is_ready && state->total_params > 0 && state->loaded_params == state->total_params && !printed_full_snapshot) {
            print_crsf_state_snapshot(state);
            printed_full_snapshot = true;
        }

        if (state->loaded_params == 0) {
            printed_full_snapshot = false;
        }

        crsf_send_device_ping();
        vTaskDelay(1);
    }
}
