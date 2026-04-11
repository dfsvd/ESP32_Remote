
#include <stdlib.h>
#include <stdio.h>
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
#define CRSF_UART_PORT   UART_NUM_1
#define CRSF_UART_TX_PIN GPIO_NUM_17
#define CRSF_UART_RX_PIN GPIO_NUM_16
#define MODE_WIFI_PIN    RC_SWITCH_3POS_UP_PIN
#define MODE_BLE_PIN     RC_SWITCH_3POS_DOWN_PIN
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
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
  
    load_settings_from_nvs();
    // Initialize button that will trigger HID reports
    const gpio_config_t boot_button_config = {
        .pin_bit_mask = BIT64(APP_BUTTON),
        .mode = GPIO_MODE_INPUT,
        .intr_type = GPIO_INTR_DISABLE,
        .pull_up_en = true,
        .pull_down_en = false,
    };
    const gpio_config_t mode_select_config = {
        .pin_bit_mask = (1ULL << MODE_WIFI_PIN) | (1ULL << MODE_BLE_PIN),
        .mode = GPIO_MODE_INPUT,
        .intr_type = GPIO_INTR_DISABLE,
        .pull_up_en = true,
        .pull_down_en = false,
    };
    crsf_config_t crsf_cfg = {
        .uart_port = CRSF_UART_PORT,
        .tx_pin = CRSF_UART_TX_PIN, 
        .rx_pin = CRSF_UART_RX_PIN, 
        .task_priority = 5,
        .task_core_id = 1,
        // 👇 把刚才写的函数名字塞进去
        .on_device_info_cb = my_crsf_device_info_callback 
    };
    ESP_ERROR_CHECK(gpio_config(&boot_button_config));
    ESP_ERROR_CHECK(gpio_config(&mode_select_config));
    ESP_LOGI(TAG, "CRSF UART pin map: TX=%d RX=%d | mode pins wifi=%d ble=%d",
             CRSF_UART_TX_PIN, CRSF_UART_RX_PIN, MODE_WIFI_PIN, MODE_BLE_PIN);
    xTaskCreatePinnedToCore( ADC_TASK, "adc_task", 4096, &joy,  4, NULL, 1 );
    crsf_init(&crsf_cfg);

    vTaskDelay(pdMS_TO_TICKS(50));


    if (gpio_get_level(MODE_WIFI_PIN) == 0)
    {
        rc_wifi_server_init(&joy);
    } else if (gpio_get_level(MODE_BLE_PIN) == 0)
    {
        ble_init(&joy);
        ble_mode = 1;
    } else {
        usb_init();
    }
    
    
    
    while (1)
    {
        if (ble_mode) 
        {
            ble_update_input(&joy);
        }

        sync_joy_to_crsf(&joy);

        if (tud_mounted())
        {
            app_send_fpv_data(&joy);
        }

        crsf_state_t *state = crsf_get_state();
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

        crsf_send_device_ping();
        vTaskDelay(1);
    }
}
