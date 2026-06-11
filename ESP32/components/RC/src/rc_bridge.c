#include "rc_bridge.h"
#include "rc_ble.h"
#include "rc_crsf.h"
#include "rc_usb_host.h"
#include "driver/uart.h"
#include "esp_err.h"
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

// ========== MSP 帧装配器 (BLE -> USB 方向) ==========
#define MSP_FRAME_MAX    264  // 6(头部) + 255(payload) + 余量
#define MSP_HEADER_LEN    6   // $ M < len cmd [payload] cksum 的最小固定部分
#define MSP_ID_SYMBOL     0x24  // '$'
#define MSP_ID_M          0x4D  // 'M'
#define MSP_DIR_REQ       0x3C  // '<' BLE->FC 请求
#define MSP_DIR_RSP       0x3E  // '>' FC->BLE 响应

typedef enum {
    MSP_STATE_IDLE = 0,
    MSP_STATE_GOT_DOLLAR,
    MSP_STATE_GOT_M,
    MSP_STATE_HAVE_DIR,
    MSP_STATE_COLLECTING,
} msp_state_t;

typedef struct {
    uint8_t  buf[MSP_FRAME_MAX];
    uint16_t pos;
    uint8_t  expected_len;   // payload 长度 (0-255)
    uint16_t frame_len;      // 完整帧总长度 (设置后为 6 + expected_len)
    msp_state_t state;
} msp_parser_t;

// 重置解析器
static void msp_parser_reset(msp_parser_t *ctx) {
    ctx->pos = 0;
    ctx->expected_len = 0;
    ctx->frame_len = 0;
    ctx->state = MSP_STATE_IDLE;
}

// 逐字节喂入, 返回 true 表示拼出一条整帧 (在 ctx->buf[0..frame_len-1])
// false = 还在收或帧错误已丢弃
static bool msp_parse_byte(msp_parser_t *ctx, uint8_t byte) {
    switch (ctx->state) {

    case MSP_STATE_IDLE:
        if (byte == MSP_ID_SYMBOL) {
            ctx->buf[0] = byte;
            ctx->pos = 1;
            ctx->state = MSP_STATE_GOT_DOLLAR;
        }
        return false;

    case MSP_STATE_GOT_DOLLAR:
        if (byte == MSP_ID_M) {
            ctx->buf[1] = byte;
            ctx->pos = 2;
            ctx->state = MSP_STATE_GOT_M;
        } else {
            ctx->state = MSP_STATE_IDLE;  // 头错, 丢弃
        }
        return false;

    case MSP_STATE_GOT_M:
        if (byte == MSP_DIR_REQ || byte == MSP_DIR_RSP) {
            ctx->buf[2] = byte;
            ctx->pos = 3;
            ctx->state = MSP_STATE_HAVE_DIR;
        } else {
            ctx->state = MSP_STATE_IDLE;
        }
        return false;

    case MSP_STATE_HAVE_DIR:
        // 这里收到的是 len 字节
        ctx->buf[3] = byte;
        ctx->expected_len = byte;
        ctx->frame_len = MSP_HEADER_LEN + byte;  // 6 + payload
        if (ctx->frame_len > MSP_FRAME_MAX) {
            ESP_LOGW(TAG, "MSP frame too long: %u > %d", ctx->frame_len, MSP_FRAME_MAX);
            msp_parser_reset(ctx);
            return false;
        }
        ctx->pos = 4;
        ctx->state = MSP_STATE_COLLECTING;
        return false;

    case MSP_STATE_COLLECTING:
        ctx->buf[ctx->pos++] = byte;
        if (ctx->pos >= ctx->frame_len) {
            // 收齐整帧, 校验 checksum
            uint8_t cksum = 0;
            for (uint16_t i = 3; i < ctx->frame_len - 1; i++) {
                cksum ^= ctx->buf[i];  // XOR(len, cmd, payload...)
            }
            if (cksum == ctx->buf[ctx->frame_len - 1]) {
                // 校验通过, 状态回 IDLE, 数据保留给调用方读取
                ctx->state = MSP_STATE_IDLE;
                ctx->pos = 0;
                return true;
            }
            ESP_LOGW(TAG, "MSP cksum fail: calc=0x%02x got=0x%02x", cksum,
                     ctx->buf[ctx->frame_len - 1]);
            msp_parser_reset(ctx);
            return false;
        }
        return false;

    default:
        msp_parser_reset(ctx);
        return false;
    }
}

// ========== UART 桥接 ==========

static bool uart_bridge_init(void) {
    const uart_config_t cfg = {
        .baud_rate = BRIDGE_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    };
    esp_err_t err = uart_param_config(BRIDGE_UART_PORT, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "UART param config fail: %s", esp_err_to_name(err));
        return false;
    }
    err = uart_set_pin(BRIDGE_UART_PORT, BRIDGE_UART_TX_PIN,
                       BRIDGE_UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "UART set pin fail: %s", esp_err_to_name(err));
        return false;
    }
    err = uart_driver_install(BRIDGE_UART_PORT, BRIDGE_UART_BUF_SIZE,
                              0, 0, NULL, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "UART driver install fail: %s", esp_err_to_name(err));
        return false;
    }
    ESP_LOGI(TAG, "UART init: port=%d TX=%d RX=%d baud=%u buf=%u",
             BRIDGE_UART_PORT, BRIDGE_UART_TX_PIN, BRIDGE_UART_RX_PIN,
             BRIDGE_UART_BAUD, BRIDGE_UART_BUF_SIZE);
    return true;
}

static void uart_bridge_deinit(void) {
    uart_driver_delete(BRIDGE_UART_PORT);
}

// ========== 桥接任务 ==========

static TaskHandle_t s_bridge_task = NULL;
static bridge_path_t s_bridge_path = BRIDGE_PATH_CRSF_MSP;

static void bridge_task(void *arg) {
    uint8_t buf[1024];
    bridge_path_t path = s_bridge_path;
    static bool     s_prev_ble_connected   = false;
    static uint32_t s_ble_connect_time     = 0;
    static uint32_t s_last_pending_req     = 0;
    static int      s_last_handled_reason  = 0;
    static msp_parser_t s_msp_parser;       // MSP 帧解析器

    if (path == BRIDGE_PATH_USB_CDC) {
        ESP_LOGI(TAG, "桥接任务已启动 (BLE NUS <-> USB CDC ACM) [MSP 帧装配]");
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
            msp_parser_reset(&s_msp_parser);
        }
        s_prev_ble_connected = ble_conn;

        if (path == BRIDGE_PATH_USB_CDC) {

            // ====== 链路健康监控 ======
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

            // 方向 A: BLE (手机) -> MSP 帧装配 -> USB (飞控)
            int n = ble_uart_read(buf, sizeof(buf));
            if (n > 0) {
                for (int i = 0; i < n; i++) {
                    if (msp_parse_byte(&s_msp_parser, buf[i])) {
                        // 凑齐一条完整 MSP 帧, 一次性写入 USB
                        usb_host_cdc_write(s_msp_parser.buf, s_msp_parser.frame_len);
                        s_last_pending_req = now;
                        uint8_t cmd = s_msp_parser.buf[4];
                        uint8_t len = s_msp_parser.buf[3];
                        ESP_LOGI(TAG, "-> USB: MSP cmd=%d len=%d", cmd, len);
                    }
                }
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

        } else if (path == BRIDGE_PATH_UART) {
            // ====== 路径: 纯串口 (原始字节转发) ======

            // 方向 A: BLE (手机) -> UART (飞控)
            int n = ble_uart_read(buf, sizeof(buf));
            if (n > 0) {
                uart_write_bytes(BRIDGE_UART_PORT, buf, n);
                s_last_pending_req = now;
                ESP_LOGI(TAG, "-> UART: %d bytes", n);
            }

            // 方向 B: UART (飞控) -> BLE (手机): MTU 分片 + 拥塞控制 (同 USB CDC)
            n = uart_read_bytes(BRIDGE_UART_PORT, buf, sizeof(buf), 0);
            if (n > 0) {
                s_last_pending_req = 0;
                int sent = 0;
                while (sent < n) {
                    int chunk = (n - sent > BLE_SAFE_PAYLOAD) ? BLE_SAFE_PAYLOAD : (n - sent);
                    ble_uart_send(buf + sent, chunk);
                    sent += chunk;
                    vTaskDelay(pdMS_TO_TICKS(1));
                }
                ESP_LOGI(TAG, "-> BLE: %d bytes (%d pkt)", n,
                         (n + BLE_SAFE_PAYLOAD - 1) / BLE_SAFE_PAYLOAD);
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

    if (path == BRIDGE_PATH_UART) {
        if (!uart_bridge_init()) {
            ESP_LOGE(TAG, "UART 桥接初始化失败, 放弃启动任务");
            return;
        }
    } else if (path == BRIDGE_PATH_CRSF_MSP) {
        // CRSF 由调用方在外部初始化
    }
    // BRIDGE_PATH_USB_CDC: USB Host 由调用方在外部初始化

    s_bridge_path = path;
    xTaskCreatePinnedToCore(bridge_task, "bridge", BRIDGE_TASK_STACK, NULL,
                            BRIDGE_TASK_PRIORITY, &s_bridge_task, 1);
    ESP_LOGI(TAG, "桥接任务已创建 (path=%s)",
             path == BRIDGE_PATH_USB_CDC ? "USB_CDC" :
             path == BRIDGE_PATH_UART    ? "UART" : "CRSF_MSP");
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
