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
#define BLE_SAFE_PAYLOAD 240

// FC 超时判定时间窗: BLE 连接持续 ~10s 后断开 + 有未回复请求
#define FC_TIMEOUT_CONN_MIN_MS  3000
#define FC_TIMEOUT_CONN_MAX_MS  25000

static TaskHandle_t s_bridge_task = NULL;
static bridge_path_t s_bridge_path = BRIDGE_PATH_CRSF_MSP;

static void bridge_task(void *arg) {
    uint8_t buf[1024];
    bridge_path_t path = s_bridge_path;
    static bool     s_prev_ble_connected   = false;
    static uint32_t s_ble_connect_time     = 0;  // BLE 连接建立时刻
    static uint32_t s_last_pending_req     = 0;  // BLE->USB 转发后尚未收到飞控回复
    static int      s_last_handled_reason  = 0;

    if (path == BRIDGE_PATH_USB_CDC) {
        ESP_LOGI(TAG, "桥接任务已启动 (BLE NUS <-> USB CDC ACM)");
    } else {
        ESP_LOGI(TAG, "桥接任务已启动 (BLE NUS <-> CRSF MSP)");
    }

    while (1) {
        uint32_t now = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);

        // ---- 检测 BLE 连接边沿 ----
        bool ble_conn = ble_is_connected();
        if (ble_conn && !s_prev_ble_connected) {
            s_ble_connect_time = now;
            s_last_pending_req = 0;
            s_last_handled_reason = 0;
        }
        s_prev_ble_connected = ble_conn;

        if (path == BRIDGE_PATH_USB_CDC) {

            // ====== 链路健康监控 ======
            // FC 超时模式:
            //   1. BLE->USB 发了请求 (s_last_pending_req != 0)
            //   2. 飞控没回复 (标记未清除)
            //   3. BLE 连接持续 ~10s 后断开
            if (!ble_conn && s_last_pending_req != 0) {
                int reason = ble_get_last_disconnect_reason();
                if (reason != s_last_handled_reason) {
                    uint32_t conn_ms = now - s_ble_connect_time;
                    s_last_handled_reason = reason;
                    s_last_pending_req = 0;
                    if (conn_ms >= FC_TIMEOUT_CONN_MIN_MS && conn_ms <= FC_TIMEOUT_CONN_MAX_MS) {
                        ESP_LOGW(TAG, "FC 超时: conn=%ums reason=%d — 重置 USB+NUS", conn_ms, reason);
                        ble_reset_nus_stream();
                        usb_host_cdc_reset();
                    } else {
                        ESP_LOGI(TAG, "BLE 断开(conn=%ums reason=%d) — 非超时跳过", conn_ms, reason);
                    }
                    vTaskDelay(pdMS_TO_TICKS(10));
                    continue;
                }
            }

            // ====== 路径: USB CDC ACM ======

            // 方向 A: BLE (手机) -> USB (飞控)
            int n = ble_uart_read(buf, sizeof(buf));
            if (n > 0) {
                s_last_pending_req = now;  // 标记: 等待飞控回复
                ESP_LOGI(TAG, "-> USB: %d bytes (pending)", n);
                usb_host_cdc_write(buf, (size_t)n);
            }

            // 方向 B: USB (飞控) -> BLE (手机): MTU 分片 + 拥塞控制
            if (usb_host_cdc_available()) {
                uint8_t usb_buf[512];
                n = usb_host_cdc_read(usb_buf, sizeof(usb_buf));
                if (n > 0) {
                    s_last_pending_req = 0;  // 飞控回复了, 清除等待
                    int sent = 0;
                    while (sent < n) {
                        int chunk = (n - sent > BLE_SAFE_PAYLOAD) ? BLE_SAFE_PAYLOAD : (n - sent);
                        ble_uart_send(usb_buf + sent, chunk);
                        sent += chunk;
                        vTaskDelay(pdMS_TO_TICKS(1));
                    }
                    ESP_LOGI(TAG, "-> BLE: %d bytes (%d pkt)", n,
                             (n + BLE_SAFE_PAYLOAD - 1) / BLE_SAFE_PAYLOAD);
                }
            }

        } else {
            // ====== 路径: CRSF MSP ======

            // 方向 A: BLE (手机) -> CRSF (飞控)
            int n = ble_uart_read(buf, sizeof(buf));
            if (n > 0) {
                ESP_LOGI(TAG, "-> CRSF: %d bytes [%02x %02x %02x ...]", n,
                         n >= 1 ? buf[0] : 0, n >= 2 ? buf[1] : 0, n >= 3 ? buf[2] : 0);
                crsf_send_msp(buf, (size_t)n);
            }

            // 方向 B: CRSF (飞控) -> BLE (手机)
            if (crsf_msp_available()) {
                n = crsf_read_msp(buf, sizeof(buf));
                if (n > 0) {
                    ESP_LOGI(TAG, "-> BLE: %d bytes [%02x %02x %02x ...]", n,
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
