#include "rc_usb_host.h"
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"

// USB Host 方式 (需要 VBUS 5V 输出)
#include "tusb.h"
// TU_API_SYNC 宏定义在 usbh.h 中，必须在 cdc_host.h 之前引入
#include "host/usbh.h"
#include "host/hcd.h"
#include "class/cdc/cdc.h"
#include "class/cdc/cdc_host.h"
#include "esp_private/usb_phy.h"
#include "dwc2_forward.h"

static const char *TAG = "USB_HOST";

// ---- CDC ACM 接收环形缓冲 ----
#define CDC_RINGBUF_SIZE 2048
static uint8_t s_cdc_rx_buf[CDC_RINGBUF_SIZE];
static volatile size_t s_cdc_rx_head = 0;
static volatile size_t s_cdc_rx_tail = 0;
static volatile bool s_cdc_mounted = false;
static uint8_t s_bulk_out_ep = 0;    // 手动打开的端点地址
static uint8_t s_bulk_in_ep = 0;
static uint8_t s_async_rx_buf[64];
static uint8_t s_usb_tx_buf[64];    // DMA 安全 TX 缓冲 (不能放 Flash 中的 const 数据)

static void cdc_ringbuf_write(const uint8_t *data, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        size_t next = (s_cdc_rx_head + 1) % CDC_RINGBUF_SIZE;
        if (next == s_cdc_rx_tail) {
            ESP_LOGW(TAG, "CDC RX 环形缓冲溢出！丢 %zu 字节", len - i);
            return;
        }
        s_cdc_rx_buf[s_cdc_rx_head] = data[i];
        s_cdc_rx_head = next;
    }
}

static size_t cdc_ringbuf_read(uint8_t *buf, size_t max_len) {
    size_t read = 0;
    while (read < max_len && s_cdc_rx_tail != s_cdc_rx_head) {
        buf[read++] = s_cdc_rx_buf[s_cdc_rx_tail];
        s_cdc_rx_tail = (s_cdc_rx_tail + 1) % CDC_RINGBUF_SIZE;
    }
    return read;
}

static size_t cdc_ringbuf_available(void) {
    if (s_cdc_rx_head >= s_cdc_rx_tail)
        return s_cdc_rx_head - s_cdc_rx_tail;
    return CDC_RINGBUF_SIZE - (s_cdc_rx_tail - s_cdc_rx_head);
}

static void cdc_ringbuf_reset(void) {
    s_cdc_rx_head = 0;
    s_cdc_rx_tail = 0;
}

// USB 轮询任务 (每 5ms 调用 tuh_task 处理 USB 事件)
static void usb_poll_task(void *arg) {
    (void)arg;
    while (1) {
        usb_host_cdc_poll();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

// ============= TinyUSB Host CDC ACM 回调 =============

void tuh_cdc_mount_cb(uint8_t idx) {
    ESP_LOGI(TAG, "CDC ACM 设备挂载 idx=%d", idx);
    s_cdc_mounted = true;
    cdc_ringbuf_reset();

    // 延迟 500ms 再初始化，让飞控 USB 栈先稳定
    // 实际初始化移至 poll 函数中延迟执行
    ESP_LOGI(TAG, "挂载完成，延迟初始化... (等待设备就绪)");
}

void tuh_cdc_umount_cb(uint8_t idx) {
    ESP_LOGW(TAG, "CDC ACM 设备卸载 idx=%d", idx);
    s_cdc_mounted = false;
}

void tuh_cdc_rx_cb(uint8_t idx) {
    uint32_t avail = tuh_cdc_read_available(idx);
    if (avail == 0) {
        ESP_LOGI(TAG, "CDC RX CB 触发但无数据 (avail=0)");
        return;
    }

    uint8_t buf[64];
    uint32_t len = tuh_cdc_read(idx, buf, (uint32_t)sizeof(buf));
    if (len > 0) {
        cdc_ringbuf_write(buf, (size_t)len);
        ESP_LOGI(TAG, "CDC RX: %lu bytes from FC [%02x %02x %02x ...]",
                 (unsigned long)len,
                 len >= 1 ? buf[0] : 0, len >= 2 ? buf[1] : 0, len >= 3 ? buf[2] : 0);
    }
}

// Bulk TX 完成回调
static void usb_host_tx_cb(tuh_xfer_t *xfer) {
    if (xfer->result != XFER_RESULT_SUCCESS) {
        ESP_LOGE(TAG, "Bulk TX 失败! result=%d (端点可能不存在)", xfer->result);
    }
}

// 异步 Bulk IN 完成回调 — 飞控回复数据后调用
static void usb_host_rx_cb(tuh_xfer_t *xfer) {
    if (xfer->result == XFER_RESULT_SUCCESS && xfer->actual_len > 0) {
        // xfer->buffer 在回调中可能为 NULL, 数据实际在 s_async_rx_buf 里
        cdc_ringbuf_write(s_async_rx_buf, xfer->actual_len);
        ESP_LOGI(TAG, "Bulk RX: %lu bytes [%02x %02x %02x ...]",
                 (unsigned long)xfer->actual_len,
                 s_async_rx_buf[0], s_async_rx_buf[1], s_async_rx_buf[2]);
    }

    // 重挂 IN 传输，保持连续接收
    if (s_cdc_mounted && s_bulk_in_ep) {
        tuh_xfer_t in_xfer = {
            .daddr = xfer->daddr,
            .ep_addr = s_bulk_in_ep,
            .buffer = s_async_rx_buf,
            .buflen = sizeof(s_async_rx_buf),
            .complete_cb = usb_host_rx_cb,
            .user_data = 0,
        };
        tuh_edpt_xfer(&in_xfer);
    }
}

// ============= 公开 API =============

void usb_host_cdc_init(void) {
    ESP_LOGI(TAG, "初始化 USB Host (CDC ACM 透穿)");

    // 1. 配置 USB PHY 为 Host 模式
    usb_phy_handle_t phy_handle = NULL;
    const usb_phy_config_t phy_config = {
        .controller = USB_PHY_CTRL_OTG,
        .target = USB_PHY_TARGET_INT,
        .otg_mode = USB_OTG_MODE_HOST,
        .otg_speed = USB_PHY_SPEED_FULL,
    };
    esp_err_t err = usb_new_phy(&phy_config, &phy_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "PHY init failed: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "USB PHY -> HOST mode");

    // 2. 启动 TinyUSB Host 栈
    const tusb_rhport_init_t host_init = {
        .role = TUSB_ROLE_HOST,
        .speed = TUSB_SPEED_FULL,
    };
    if (!tuh_rhport_init(0, &host_init)) {
        ESP_LOGE(TAG, "tuh_rhport_init(0) 失败");
        return;
    }
    ESP_LOGI(TAG, "TinyUSB Host 栈已启动（等待飞控 USB 接入...）");
    ESP_LOGI(TAG, "tuh_inited=%d (期望 1)", tuh_inited());

    // 3. 启动 USB 轮询任务 (每秒 200 次, 5ms 间隔)
    // 之前由 bridge_start 创建, 现在透穿模式独立运行
    xTaskCreatePinnedToCore(usb_poll_task, "usb_poll", 4096, NULL, 3, NULL, 0);
    ESP_LOGI(TAG, "USB 轮询任务已启动");

    cdc_ringbuf_reset();
}

void usb_host_cdc_deinit(void) {
    s_cdc_mounted = false;
    cdc_ringbuf_reset();
    ESP_LOGI(TAG, "USB Host 已反初始化");
}

bool usb_host_cdc_connected(void) {
    return s_cdc_mounted;
}

int usb_host_cdc_read(uint8_t *buf, size_t max_len) {
    if (!buf || max_len == 0) return 0;
    return (int)cdc_ringbuf_read(buf, max_len);
}

int usb_host_cdc_write(const uint8_t *data, size_t len) {
    if (!data || len == 0 || !usb_host_cdc_connected() || !s_bulk_out_ep) return 0;
    if (len > sizeof(s_usb_tx_buf)) len = sizeof(s_usb_tx_buf);

    // DMA 无法直接读取 Flash const 数据，必须拷贝到 RAM
    memcpy(s_usb_tx_buf, data, len);

    tuh_xfer_t out_xfer = {
        .daddr = 1,
        .ep_addr = s_bulk_out_ep,
        .buffer = s_usb_tx_buf,
        .buflen = (uint16_t)len,
        .complete_cb = usb_host_tx_cb,
        .user_data = 0,
    };
    bool ok = tuh_edpt_xfer(&out_xfer);

    ESP_LOGI(TAG, "Bulk TX: %s (%zu bytes -> EP 0x%02x)",
             ok ? "OK" : "FAIL", len, s_bulk_out_ep);
    return ok ? (int)len : 0;
}

bool usb_host_cdc_available(void) {
    return cdc_ringbuf_available() > 0;
}

void usb_host_cdc_poll(void) {
    static uint32_t s_last_log = 0;
    static bool s_test_sent = false;
    static bool s_cdc_inited = false;
    uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;

    // tuh_task() 内部 UINT32_MAX 会永久阻塞, 改用 100ms 超时
    // 足以完成设备枚举, 同时保证轮询任务不被卡死
    tuh_task_ext(100, false);

    // 挂载后延迟初始化 (500ms 后执行 CDC ACM 配置)
    if (s_cdc_mounted && !s_cdc_inited && now > 6500) {
        s_cdc_inited = true;
        uint8_t daddr = 1;  // 第一个 USB 设备

        ESP_LOGI(TAG, "=== 延迟初始化 CDC ACM ===");

        // SET_LINE_CODING (115200 8N1)
        cdc_line_coding_t line_coding = {
            .bit_rate = 115200,
            .stop_bits = CDC_LINE_CODING_STOP_BITS_1,
            .parity = CDC_LINE_CODING_PARITY_NONE,
            .data_bits = 8,
        };
        tusb_xfer_result_t result = tuh_cdc_set_line_coding_sync(0, &line_coding);
        ESP_LOGI(TAG, "SET_LINE_CODING: %s", result == XFER_RESULT_SUCCESS ? "OK" : "FAIL");

        // DTR/RTS
        result = tuh_cdc_connect_sync(0);
        ESP_LOGI(TAG, "DTR/RTS: %s", result == XFER_RESULT_SUCCESS ? "OK" : "FAIL");

        // 手动打开 Bulk 端点 (CDC ACM 驱动未找到数据接口)
        uint8_t ep_out_addr = 0x01, ep_in_addr = 0x81; // STM32 VCP 标准
        // 如果上面组合不通, 尝试 STM32 VCP 标准: 0x01/0x81 或 0x03/0x83

        tusb_desc_endpoint_t ep_out = {
            .bLength = sizeof(tusb_desc_endpoint_t),
            .bDescriptorType = TUSB_DESC_ENDPOINT,
            .bEndpointAddress = ep_out_addr,
            .bmAttributes = { .xfer = TUSB_XFER_BULK },
            .wMaxPacketSize = 64,
        };
        ESP_LOGI(TAG, "Bulk OUT 0x%02X (MPS=64): %s", ep_out_addr,
                 tuh_edpt_open(daddr, &ep_out) ? "OK" : "FAIL");

        tusb_desc_endpoint_t ep_in = {
            .bLength = sizeof(tusb_desc_endpoint_t),
            .bDescriptorType = TUSB_DESC_ENDPOINT,
            .bEndpointAddress = ep_in_addr,
            .bmAttributes = { .xfer = TUSB_XFER_BULK },
            .wMaxPacketSize = 64,
        };
        bool in_opened = tuh_edpt_open(daddr, &ep_in);
        ESP_LOGI(TAG, "Bulk IN 0x%02X: %s", ep_in_addr, in_opened ? "OK" : "FAIL");

        s_bulk_out_ep = ep_out_addr;
        s_bulk_in_ep = ep_in_addr;

        // 首次扣动 IN 轮询扳机 — 没有这个接收链路永远不会启动
        if (in_opened) {
            tuh_xfer_t in_xfer = {
                .daddr = daddr,
                .ep_addr = s_bulk_in_ep,
                .buffer = s_async_rx_buf,
                .buflen = sizeof(s_async_rx_buf),
                .complete_cb = usb_host_rx_cb,
            };
            ESP_LOGI(TAG, "后台 Bulk IN 轮询启动: %s",
                     tuh_edpt_xfer(&in_xfer) ? "OK" : "FAIL");
        }
    }

    // 每 5 秒打印一次状态
    if (now - s_last_log > 5000) {
        s_last_log = now;
        bool port_connected = hcd_port_connect_status(0);
        ESP_LOGI(TAG, "USB 状态: inited=%d mounted=%d port_connected=%d",
                 tuh_inited(), s_cdc_mounted, port_connected);
        ESP_LOGI(TAG, "CDC ringbuf: %zu bytes", cdc_ringbuf_available());
    }

    // ---- MSP 协议解析状态机 ----
    // 持续从 ringbuf 读取字节并拼装完整的 MSP 帧
    {
        typedef enum {
            MSP_IDLE, MSP_HEADER_M, MSP_HEADER_ARROW,
            MSP_SIZE, MSP_CMD, MSP_PAYLOAD, MSP_CHECKSUM
        } msp_parse_state_t;
        static msp_parse_state_t s_parse = MSP_IDLE;
        static uint8_t s_parse_buf[256];
        static uint8_t s_parse_len = 0, s_parse_idx = 0, s_parse_cmd = 0, s_parse_csum = 0;

        uint8_t byte;
        while (cdc_ringbuf_read(&byte, 1) > 0) {
            switch (s_parse) {
            case MSP_IDLE:
                if (byte == '$') s_parse = MSP_HEADER_M;
                break;
            case MSP_HEADER_M:
                if (byte == 'M') s_parse = MSP_HEADER_ARROW;
                else s_parse = MSP_IDLE;
                break;
            case MSP_HEADER_ARROW:
                if (byte == '>') s_parse = MSP_SIZE;
                else s_parse = MSP_IDLE;
                break;
            case MSP_SIZE:
                s_parse_len = byte;
                s_parse_csum = byte;
                s_parse_idx = 0;
                s_parse = MSP_CMD;
                break;
            case MSP_CMD:
                s_parse_cmd = byte;
                s_parse_csum ^= byte;
                s_parse = (s_parse_len > 0) ? MSP_PAYLOAD : MSP_CHECKSUM;
                break;
            case MSP_PAYLOAD:
                s_parse_buf[s_parse_idx++] = byte;
                s_parse_csum ^= byte;
                if (s_parse_idx >= s_parse_len) s_parse = MSP_CHECKSUM;
                break;
            case MSP_CHECKSUM: {
                bool ok = (s_parse_csum == byte);
                // 组装完整帧 hex 用于对比
                char hex[256] = {0};
                int p = 0;
                p += sprintf(hex + p, "%02x%02x%02x%02x%02x",
                             0x24, 0x4D, 0x3E, s_parse_len, s_parse_cmd);
                for (int i = 0; i < s_parse_len && p < (int)sizeof(hex)-6; i++)
                    p += sprintf(hex + p, "%02x", s_parse_buf[i]);
                sprintf(hex + p, "%02x", byte);
                ESP_LOGI(TAG, "RX: %s (%d bytes) %s", hex,
                         6 + s_parse_len, ok ? "✓" : "CRC_FAIL");
                s_parse = MSP_IDLE;
                break;
            }
            }
        }
    }

    // 逐条发送 MSP 命令 (基于响应驱动的状态机)
    typedef struct {
        const char* name;
        uint8_t data[6];
    } msp_cmd_t;
    static const msp_cmd_t s_cmds[] = {
        {"API_VERSION", {0x24,0x4D,0x3C,0x00,0x01,0x01}},
        {"FC_VARIANT",  {0x24,0x4D,0x3C,0x00,0x02,0x02}},
        {"FC_VERSION",  {0x24,0x4D,0x3C,0x00,0x03,0x03}},
        {"BOARD_INFO",  {0x24,0x4D,0x3C,0x00,0x04,0x04}},
        {"BUILD_INFO",  {0x24,0x4D,0x3C,0x00,0x05,0x05}},
        {"NAME",        {0x24,0x4D,0x3C,0x00,0x0A,0x0A}},
    };
    static int s_cmd_idx = 0;
    static bool s_sent = false;

    if (s_cdc_inited && !s_test_sent && s_cmd_idx < 6) {
        if (!s_sent) {
            ESP_LOGI(TAG, "========== [%d/6] %s ==========",
                     s_cmd_idx + 1, s_cmds[s_cmd_idx].name);
            ESP_LOGI(TAG, "TX: 244d3c%02x%02x%02x",
                     s_cmds[s_cmd_idx].data[3],
                     s_cmds[s_cmd_idx].data[4],
                     s_cmds[s_cmd_idx].data[5]);
            usb_host_cdc_write(s_cmds[s_cmd_idx].data, 6);
            s_sent = true;
        } else if (cdc_ringbuf_available() > 0 || now > 10000 + s_cmd_idx * 2000) {
            // ringbuf 有数据 或 超时 → 发下一条
            s_cmd_idx++;
            s_sent = false;
        }
        if (s_cmd_idx >= 6) {
            s_test_sent = true;
            ESP_LOGI(TAG, "========== MSP 测试完毕 ==========");
        }
    }
}
