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
#include "freertos/stream_buffer.h"

static const char *TAG = "USB_HOST";

// ---- CDC ACM 接收环形缓冲 ----
#define CDC_RINGBUF_SIZE 2048
static uint8_t s_cdc_rx_buf[CDC_RINGBUF_SIZE];
static volatile size_t s_cdc_rx_head = 0;
static volatile size_t s_cdc_rx_tail = 0;
static volatile bool s_cdc_mounted = false;
static bool s_cdc_inited = false;
static uint32_t s_mount_time = 0;
static uint8_t s_cdc_dev_addr = 0;          // 飞控 USB 设备地址 (动态获取)
static uint8_t s_bulk_out_ep = 0;
static uint8_t s_bulk_in_ep = 0;
static uint8_t s_async_rx_buf[64];
static uint8_t s_usb_tx_buf[64];
static volatile bool s_bulk_out_busy = false;
static volatile bool s_bulk_in_inflight = false;
static StreamBufferHandle_t s_tx_stream = NULL; // 跨任务 TX 队列

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
    // 获取动态设备地址 (热插拔后可能变化)
    tuh_itf_info_t info;
    s_cdc_dev_addr = tuh_cdc_itf_get_info(idx, &info) ? info.daddr : 1;
    ESP_LOGI(TAG, "CDC ACM 挂载 idx=%d daddr=%d", idx, s_cdc_dev_addr);
    s_cdc_mounted = true;
    s_cdc_inited = false;
    s_mount_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    cdc_ringbuf_reset();
}

void tuh_cdc_umount_cb(uint8_t idx) {
    ESP_LOGW(TAG, "CDC ACM 设备卸载 idx=%d", idx);
    s_cdc_mounted = false;
    s_cdc_inited = false;
    s_bulk_out_busy = false;
    s_bulk_in_inflight = false;
    s_bulk_out_ep = 0;
    s_bulk_in_ep = 0;
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
    s_bulk_out_busy = false; // 释放 TX 锁
    if (xfer->result != XFER_RESULT_SUCCESS) {
        ESP_LOGD(TAG, "Bulk TX 异常: result=%d", xfer->result);
    }
}

// 异步 Bulk IN 完成回调 — 飞控回复数据后调用
static void usb_host_rx_cb(tuh_xfer_t *xfer) {
    s_bulk_in_inflight = false; // 标记 RX 链落地

    if (xfer->result == XFER_RESULT_SUCCESS && xfer->actual_len > 0) {
        cdc_ringbuf_write(s_async_rx_buf, xfer->actual_len);
    } else if (xfer->result != XFER_RESULT_SUCCESS) {
        ESP_LOGD(TAG, "Bulk RX 异常: result=%d", xfer->result);
    }

    // 重挂 IN 传输
    if (s_cdc_mounted && s_bulk_in_ep) {
        tuh_xfer_t in_xfer = {
            .daddr = xfer->daddr,
            .ep_addr = s_bulk_in_ep,
            .buffer = s_async_rx_buf,
            .buflen = sizeof(s_async_rx_buf),
            .complete_cb = usb_host_rx_cb,
            .user_data = 0,
        };
        s_bulk_in_inflight = tuh_edpt_xfer(&in_xfer);
    }
}

// ============= 公开 API =============

void usb_host_cdc_init(void) {
    ESP_LOGI(TAG, "初始化 USB Host (CDC ACM 透穿)");

    // 创建 TX 流缓冲 (跨任务安全)
    if (s_tx_stream == NULL) {
        s_tx_stream = xStreamBufferCreate(1024, 1);
    }

    // 1. 冷启动修复：先拉低 USB 总线 (GPIO 19=D-, 20=D+)，模拟设备未插入
    //    解决插着线开机/烧录后 PHY 检测不到上升沿的问题
    gpio_reset_pin(19);
    gpio_reset_pin(20);
    gpio_set_direction(19, GPIO_MODE_OUTPUT);
    gpio_set_direction(20, GPIO_MODE_OUTPUT);
    gpio_set_level(19, 0);
    gpio_set_level(20, 0);
    vTaskDelay(pdMS_TO_TICKS(20));

    // 2. 配置 USB PHY 为 Host 模式 (接管 GPIO 19/20)
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
    if (!data || len == 0 || !s_tx_stream) return 0;
    // 推入队列, poll 任务会取出发送, 避免多任务并发调 TinyUSB
    size_t n = xStreamBufferSend(s_tx_stream, data, len, pdMS_TO_TICKS(10));
    if (n < len) ESP_LOGW(TAG, "TX 队列满, 丢 %zu 字节", len - n);
    return (int)n;
}

bool usb_host_cdc_available(void) {
    return cdc_ringbuf_available() > 0;
}

void usb_host_cdc_poll(void) {
    static uint32_t s_last_log = 0;
    uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;

    tuh_task_ext(100, false);

    // Poll 任务内消费 TX 队列 (唯一调 tuh_edpt_xfer 的地方)
    if (s_cdc_mounted && s_cdc_inited && s_bulk_out_ep && !s_bulk_out_busy) {
        if (xStreamBufferBytesAvailable(s_tx_stream) > 0) {
            size_t n = xStreamBufferReceive(s_tx_stream, s_usb_tx_buf, sizeof(s_usb_tx_buf), 0);
            if (n > 0) {
                s_bulk_out_busy = true;
                tuh_xfer_t xfer = {
                    .daddr = s_cdc_dev_addr,
                    .ep_addr = s_bulk_out_ep,
                    .buffer = s_usb_tx_buf,
                    .buflen = (uint16_t)n,
                    .complete_cb = usb_host_tx_cb,
                };
                if (!tuh_edpt_xfer(&xfer)) s_bulk_out_busy = false;
                ESP_LOGI(TAG, "TX: %zu bytes -> EP 0x%02x", n, s_bulk_out_ep);
            }
        }
    }

    // 挂载后至少 500ms 才开始初始化
    if (s_cdc_mounted && !s_cdc_inited && (now - s_mount_time > 500)) {
        s_cdc_inited = true;
        uint8_t daddr = s_cdc_dev_addr;

        ESP_LOGI(TAG, "=== 延迟初始化 CDC ACM (daddr=%d)===", daddr);

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

        // 首次扣动 IN 轮询扳机
        if (in_opened) {
            tuh_xfer_t in_xfer = {
                .daddr = daddr,
                .ep_addr = s_bulk_in_ep,
                .buffer = s_async_rx_buf,
                .buflen = sizeof(s_async_rx_buf),
                .complete_cb = usb_host_rx_cb,
            };
            s_bulk_in_inflight = tuh_edpt_xfer(&in_xfer);
            ESP_LOGI(TAG, "后台 Bulk IN 轮询启动: %s",
                     s_bulk_in_inflight ? "OK" : "FAIL");
        }
    }

    // RX 引擎看门狗: 链条断裂超过 200ms 时重启
    if (s_cdc_mounted && s_cdc_inited && s_bulk_in_ep) {
        static uint32_t s_last_kick = 0;
        if (!s_bulk_in_inflight && now - s_last_kick > 200) {
            s_last_kick = now;
            tuh_xfer_t in_xfer = {
                .daddr = s_cdc_dev_addr,
                .ep_addr = s_bulk_in_ep,
                .buffer = s_async_rx_buf,
                .buflen = sizeof(s_async_rx_buf),
                .complete_cb = usb_host_rx_cb,
            };
            s_bulk_in_inflight = tuh_edpt_xfer(&in_xfer);
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

}
