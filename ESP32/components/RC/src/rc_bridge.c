#include "rc_bridge.h"
#include "rc_ble.h"
#include "rc_crsf.h"
#include "rc_usb_host.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "BRIDGE";
#define BRIDGE_TASK_STACK 4096
#define BRIDGE_TASK_PRIORITY 4
#define BRIDGE_POLL_MS 10
#define USB_POLL_TASK_STACK 4096
#define USB_POLL_TASK_PRIORITY 3

static TaskHandle_t s_bridge_task = NULL;
static TaskHandle_t s_usb_poll_task = NULL;
static bridge_path_t s_bridge_path = BRIDGE_PATH_CRSF_MSP;

static void usb_poll_task(void *arg) {
    (void)arg;
    ESP_LOGI(TAG, "USB 轮询任务已启动");
    while (1) {
        usb_host_cdc_poll();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

static void bridge_task(void *arg) {
    uint8_t buf[1024];
    bridge_path_t path = s_bridge_path;

    if (path == BRIDGE_PATH_USB_CDC) {
        ESP_LOGI(TAG, "桥接任务已启动 (BLE NUS ↔ USB CDC ACM)");
    } else {
        ESP_LOGI(TAG, "桥接任务已启动 (BLE NUS ↔ CRSF MSP)");
    }

    while (1) {
        if (path == BRIDGE_PATH_USB_CDC) {
            // ====== 路径: USB CDC ACM ======

            // 方向 A: BLE (手机) → USB (飞控)
            int n = ble_uart_read(buf, sizeof(buf));
            if (n > 0) {
                ESP_LOGI(TAG, "→ USB: %d bytes [%02x %02x %02x ...]", n,
                         n >= 1 ? buf[0] : 0, n >= 2 ? buf[1] : 0, n >= 3 ? buf[2] : 0);
                usb_host_cdc_write(buf, (size_t)n);
            }

            // 方向 B: USB (飞控) → BLE (手机)
            if (usb_host_cdc_available()) {
                n = usb_host_cdc_read(buf, sizeof(buf));
                if (n > 0) {
                    ESP_LOGI(TAG, "→ BLE: %d bytes [%02x %02x %02x ...]", n,
                             n >= 1 ? buf[0] : 0, n >= 2 ? buf[1] : 0, n >= 3 ? buf[2] : 0);
                    ble_uart_send(buf, (size_t)n);
                }
            }

        } else {
            // ====== 路径: CRSF MSP ======

            // 方向 A: BLE (手机) → CRSF (飞控)
            int n = ble_uart_read(buf, sizeof(buf));
            if (n > 0) {
                ESP_LOGI(TAG, "→ CRSF: %d bytes [%02x %02x %02x ...]", n,
                         n >= 1 ? buf[0] : 0, n >= 2 ? buf[1] : 0, n >= 3 ? buf[2] : 0);
                crsf_send_msp(buf, (size_t)n);
            }

            // 方向 B: CRSF (飞控) → BLE (手机)
            if (crsf_msp_available()) {
                n = crsf_read_msp(buf, sizeof(buf));
                if (n > 0) {
                    ESP_LOGI(TAG, "→ BLE: %d bytes [%02x %02x %02x ...]", n,
                             n >= 1 ? buf[0] : 0, n >= 2 ? buf[1] : 0, n >= 3 ? buf[2] : 0);
                    ble_uart_send(buf, (size_t)n);
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(BRIDGE_POLL_MS));
    }
}

void bridge_start(bridge_path_t path) {
    if (s_bridge_task != NULL) {
        ESP_LOGW(TAG, "桥接任务已在运行");
        return;
    }
    s_bridge_path = path;
    xTaskCreatePinnedToCore(bridge_task, "bridge", BRIDGE_TASK_STACK, NULL,
                            BRIDGE_TASK_PRIORITY, &s_bridge_task, 1);
    ESP_LOGI(TAG, "桥接任务已创建 (path=%s)",
             path == BRIDGE_PATH_USB_CDC ? "USB_CDC" : "CRSF_MSP");
    if (path == BRIDGE_PATH_USB_CDC) {
        // USB 轮询任务放到独立任务，避免 bridge 卡顿
        xTaskCreatePinnedToCore(usb_poll_task, "usb_poll", USB_POLL_TASK_STACK, NULL,
                                USB_POLL_TASK_PRIORITY, &s_usb_poll_task, 0);
    }
}

void bridge_stop(void) {
    if (s_bridge_task != NULL) {
        vTaskDelete(s_bridge_task);
        s_bridge_task = NULL;
        ESP_LOGI(TAG, "桥接任务已停止");
    }
}

bool bridge_is_running(void) {
    return s_bridge_task != NULL;
}
