#include "rc_usb_host.h"
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"

// TinyUSB Host 裸端点 (不依赖 CDC 类驱动)
#include "tusb.h"
#include "host/usbh.h"
#include "host/hcd.h"
#include "class/cdc/cdc.h"      // cdc_line_coding_t, CDC_REQUEST_*
#include "esp_private/usb_phy.h"
#include "dwc2_forward.h"
#include "freertos/stream_buffer.h"

static const char *TAG = "USB_HOST";

// 注意: cdc_line_coding_t + CDC_REQUEST_* 宏来自 tusb.h -> class/cdc/cdc.h
// 此处不再重复定义

// ========== 接收环形缓冲 ==========
#define CDC_RINGBUF_SIZE 2048
static uint8_t s_cdc_rx_buf[CDC_RINGBUF_SIZE];
static volatile size_t s_cdc_rx_head = 0;
static volatile size_t s_cdc_rx_tail = 0;

// ========== 设备状态 ==========
static volatile bool s_cdc_mounted   = false;
static bool          s_cdc_inited    = false;
static uint32_t      s_mount_time    = 0;
static uint8_t       s_cdc_dev_addr  = 0;

// ========== 端点配置 ==========
static uint8_t  s_bulk_out_ep = 0;
static uint8_t  s_bulk_in_ep  = 0;
static uint8_t  s_async_rx_buf[64];
static uint8_t  s_usb_tx_buf[64];

static volatile bool s_bulk_out_busy    = false;
static volatile bool s_bulk_in_inflight = false;

static StreamBufferHandle_t s_tx_stream = NULL;

// ========== 看门狗时间戳 (绝对毫秒) ==========
static uint32_t s_last_rx_cb_time   = 0;   // 最近一次 IN 回调触发 (任意结果)
static uint32_t s_last_rx_data_time = 0;   // 最近一次实际收到飞控数据
static uint32_t s_last_tx_time      = 0;   // 最近一次提交 TX 传输
static uint32_t s_last_tx_done_time = 0;   // 最近一次 Bulk OUT 完成 (节流用)

// ========== 同步控制传输信号量 ==========
static SemaphoreHandle_t s_ctrl_sem = NULL;

// =================================================================
// 环形缓冲内联函数
// =================================================================
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

// =================================================================
// TinyUSB 回调
// =================================================================

// 控制传输完成回调 (用于 cdc_ctrl_xfer_sync)
static void ctrl_complete_cb(tuh_xfer_t *xfer) {
    SemaphoreHandle_t sem = (SemaphoreHandle_t)(uintptr_t)xfer->user_data;
    if (sem) xSemaphoreGive(sem);
}

// 同步 CDC 控制传输: 提交后自旋 tuh_task 直到完成或超时
static bool cdc_ctrl_xfer_sync(uint8_t daddr, uint8_t bmRequestType,
                                uint8_t bRequest, uint16_t wValue,
                                uint16_t wIndex, void *buffer, uint16_t wLength)
{
    if (!s_ctrl_sem) {
        s_ctrl_sem = xSemaphoreCreateBinary();
        if (!s_ctrl_sem) return false;
    }
    // 清空残留信号量 (前次超时遗留)
    xSemaphoreTake(s_ctrl_sem, 0);

    // 构造控制请求包
    tusb_control_request_t const ctrl_req = {
        .bmRequestType = bmRequestType,
        .bRequest      = bRequest,
        .wValue        = wValue,
        .wIndex        = wIndex,
        .wLength       = wLength,
    };

    tuh_xfer_t xfer = {
        .daddr   = daddr,
        .ep_addr = 0,  // 端点 0 = 控制传输
        .setup   = &ctrl_req,
        .buffer      = (uint8_t*)buffer,
        // buflen 与 setup 在同一 union 中, 控制传输使用 setup->wLength
        .complete_cb = ctrl_complete_cb,
        .user_data   = (uintptr_t)s_ctrl_sem,
    };

    if (!tuh_control_xfer(&xfer)) {
        ESP_LOGE(TAG, "控制传输提交失败 (req=0x%02x)", bRequest);
        return false;
    }

    // 自旋等待完成 (最坏 500ms, 控制传输通常在 20ms 内完成)
    TickType_t start = xTaskGetTickCount();
    while ((xTaskGetTickCount() - start) < pdMS_TO_TICKS(500)) {
        // 检查是否完成
        if (xSemaphoreTake(s_ctrl_sem, 0) == pdTRUE) return true;
        // 驱动 USB 事件处理, 让控制传输走完
        tuh_task_ext(5, false);
    }

    ESP_LOGE(TAG, "控制传输超时 (req=0x%02x, %lu ms)", bRequest,
             (unsigned long)(xTaskGetTickCount() - start) * portTICK_PERIOD_MS);
    return false;
}

// ---------- Bulk TX 完成 ----------
static void usb_host_tx_cb(tuh_xfer_t *xfer) {
    s_bulk_out_busy = false;
    s_last_tx_done_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    if (xfer->result != XFER_RESULT_SUCCESS) {
        ESP_LOGD(TAG, "Bulk TX result=%d", xfer->result);
    }
}

// ---------- Bulk IN 完成 (飞控回复) ----------
static void usb_host_rx_cb(tuh_xfer_t *xfer) {
    s_bulk_in_inflight = false;
    s_last_rx_cb_time = xTaskGetTickCount() * portTICK_PERIOD_MS;

    if (xfer->result == XFER_RESULT_SUCCESS && xfer->actual_len > 0) {
        s_last_rx_data_time = s_last_rx_cb_time;
        cdc_ringbuf_write(s_async_rx_buf, xfer->actual_len);
    } else if (xfer->result != XFER_RESULT_SUCCESS) {
        ESP_LOGD(TAG, "Bulk RX result=%d", xfer->result);
    }

    // 连续轮询: 立即重新提交 IN 传输
    if (s_cdc_mounted && s_bulk_in_ep) {
        tuh_xfer_t in_xfer = {
            .daddr       = xfer->daddr,
            .ep_addr     = s_bulk_in_ep,
            .buffer      = s_async_rx_buf,
            .buflen      = sizeof(s_async_rx_buf),
            .complete_cb = usb_host_rx_cb,
            .user_data   = 0,
        };
        s_bulk_in_inflight = tuh_edpt_xfer(&in_xfer);
    }
}

// ---------- 设备级回调 (替代 CDC 类驱动回调) ----------

// 任意 USB 设备枚举完成
void tuh_mount_cb(uint8_t daddr) {
    ESP_LOGI(TAG, "USB 设备挂载 daddr=%d", daddr);
    s_cdc_dev_addr      = daddr;
    s_cdc_mounted       = true;
    s_cdc_inited        = false;
    s_bulk_out_busy     = false;   // 清洗卸载残留忙碌锁
    s_bulk_in_inflight  = false;   // 重连后从干净状态重新启动 IN 轮询
    s_mount_time        = xTaskGetTickCount() * portTICK_PERIOD_MS;
    cdc_ringbuf_reset();
}

// 总线弹跳: 强制 D+/D- 拉低再释放, 触发 PHY 重新检测设备
void usb_host_cdc_bus_bounce(void) {
    ESP_LOGW(TAG, "USB 总线弹跳 — 拉低 D+/D- 触发 PHY 重检");
    gpio_reset_pin(19);
    gpio_reset_pin(20);
    gpio_set_direction(19, GPIO_MODE_OUTPUT);
    gpio_set_direction(20, GPIO_MODE_OUTPUT);
    gpio_set_level(19, 0);
    gpio_set_level(20, 0);
    vTaskDelay(pdMS_TO_TICKS(30));
    gpio_set_direction(19, GPIO_MODE_INPUT);
    gpio_set_direction(20, GPIO_MODE_INPUT);
    gpio_reset_pin(19);
    gpio_reset_pin(20);
}

// 焦土式重置所有 USB 设备状态 + 总线弹跳 (断开、异常恢复统一入口)
void usb_host_cdc_reset(void) {
    ESP_LOGW(TAG, "USB 设备状态重置 — 清空锁/缓冲/时间戳");
    s_cdc_mounted       = false;
    s_cdc_inited        = false;
    s_bulk_out_busy     = false;
    s_bulk_in_inflight  = false;
    s_bulk_out_ep       = 0;
    s_bulk_in_ep        = 0;
    s_cdc_dev_addr      = 0;
    s_mount_time        = 0;
    s_last_tx_time      = 0;
    s_last_tx_done_time = 0;
    s_last_rx_cb_time   = 0;
    s_last_rx_data_time = 0;

    // 排空收发缓冲, 防止重连后发出残留数据
    if (s_tx_stream) xStreamBufferReset(s_tx_stream);
    cdc_ringbuf_reset();

    // 总线弹跳: 解决 port_connected=0 时 PHY 卡死无法检测插入
    usb_host_cdc_bus_bounce();
}

// 任意 USB 设备拔出
void tuh_umount_cb(uint8_t daddr) {
    ESP_LOGW(TAG, "USB 设备卸载 daddr=%d", daddr);
    (void)daddr;
    usb_host_cdc_reset();
}

// =================================================================
// USB 轮询任务 (每 5ms)
// =================================================================
static void usb_poll_task(void *arg) {
    (void)arg;
    while (1) {
        usb_host_cdc_poll();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

// =================================================================
// 公开 API
// =================================================================

void usb_host_cdc_init(void) {
    ESP_LOGI(TAG, "初始化 USB Host (CDC ACM 原生端点, 无类驱动)");

    // 创建 TX 流缓冲 + 控制传输信号量
    if (s_tx_stream == NULL) {
        s_tx_stream = xStreamBufferCreate(1024, 1);
    }
    if (s_ctrl_sem == NULL) {
        s_ctrl_sem = xSemaphoreCreateBinary();
    }

    // ---- 1. 冷启动修复 ----
    // 拉低 D+/D- 模拟设备未插入, 解决插线开机 PHY 检测不到上升沿
    gpio_reset_pin(19);
    gpio_reset_pin(20);
    gpio_set_direction(19, GPIO_MODE_OUTPUT);
    gpio_set_direction(20, GPIO_MODE_OUTPUT);
    gpio_set_level(19, 0);
    gpio_set_level(20, 0);
    vTaskDelay(pdMS_TO_TICKS(20));

    // 释放 GPIO 控制权 → 高阻输入，让 USB PHY 接管 D+/D- (否则拔插无法检测)
    gpio_set_direction(19, GPIO_MODE_INPUT);
    gpio_set_direction(20, GPIO_MODE_INPUT);
    vTaskDelay(pdMS_TO_TICKS(10));

    // ---- 2. USB PHY → Host 模式 ----
    usb_phy_handle_t phy_handle = NULL;
    const usb_phy_config_t phy_config = {
        .controller = USB_PHY_CTRL_OTG,
        .target     = USB_PHY_TARGET_INT,
        .otg_mode   = USB_OTG_MODE_HOST,
        .otg_speed  = USB_PHY_SPEED_FULL,
    };
    esp_err_t err = usb_new_phy(&phy_config, &phy_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "PHY init failed: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "USB PHY → Host mode");

    // ---- 3. 启动 TinyUSB Host 栈 (无 CDC 类驱动) ----
    const tusb_rhport_init_t host_init = {
        .role  = TUSB_ROLE_HOST,
        .speed = TUSB_SPEED_FULL,
    };
    if (!tuh_rhport_init(0, &host_init)) {
        ESP_LOGE(TAG, "tuh_rhport_init(0) 失败");
        return;
    }
    ESP_LOGI(TAG, "TinyUSB Host 栈已启动 (tuh_inited=%d)", tuh_inited());

    // ---- 4. 启动轮询任务 ----
    xTaskCreatePinnedToCore(usb_poll_task, "usb_poll", 4096, NULL, 3, NULL, 0);
    ESP_LOGI(TAG, "USB 轮询任务已启动");

    cdc_ringbuf_reset();
}

void usb_host_cdc_deinit(void) {
    usb_host_cdc_reset();
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
    size_t n = xStreamBufferSend(s_tx_stream, data, len, pdMS_TO_TICKS(10));
    if (n < len) ESP_LOGW(TAG, "TX 队列满, 丢 %zu 字节", len - n);
    return (int)n;
}

bool usb_host_cdc_available(void) {
    return cdc_ringbuf_available() > 0;
}

void usb_host_cdc_poll(void) {
    static uint32_t s_last_log = 0;

    tuh_task_ext(100, false);

    // 在 tuh_task 之后采样 now, 避免回调中更新的时间戳比 now 新导致下溢
    uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;

    // ================================================================
    // TX: 消费流缓冲 → Bulk OUT
    // ================================================================
    if (s_cdc_mounted && s_cdc_inited && s_bulk_out_ep && !s_bulk_out_busy
        && (now - s_last_tx_done_time >= 3)) {        // 3ms 节流: 给飞控 FIFO 喘息时间
        if (xStreamBufferBytesAvailable(s_tx_stream) > 0) {
            size_t n = xStreamBufferReceive(s_tx_stream, s_usb_tx_buf,
                                            sizeof(s_usb_tx_buf), 0);
            if (n > 0) {
                s_bulk_out_busy = true;
                tuh_xfer_t xfer = {
                    .daddr   = s_cdc_dev_addr,
                    .ep_addr = s_bulk_out_ep,
                    .buffer  = s_usb_tx_buf,
                    .buflen  = (uint16_t)n,
                    .complete_cb = usb_host_tx_cb,
                };
                if (tuh_edpt_xfer(&xfer)) {
                    s_last_tx_time = now;
                } else {
                    s_bulk_out_busy = false;
                }
                ESP_LOGI(TAG, "TX: %zu bytes -> EP 0x%02x", n, s_bulk_out_ep);
            }
        }
    }

    // ================================================================
    // 延迟初始化: 挂载后 500ms 配置 CDC ACM 端点
    // ================================================================
    if (s_cdc_mounted && !s_cdc_inited && (now - s_mount_time > 500)) {
        s_cdc_inited = true;
        uint8_t daddr = s_cdc_dev_addr;

        ESP_LOGI(TAG, "=== 初始化 CDC ACM 端点 (daddr=%d, 无 CDC 类驱动) ===", daddr);

        // ---- SET_LINE_CODING: 115200 8N1 ----
        cdc_line_coding_t line_coding = {
            .bit_rate  = 115200,
            .stop_bits = 0,     // 1 stop bit
            .parity    = 0,     // none
            .data_bits = 8,
        };
        bool ok = cdc_ctrl_xfer_sync(daddr,
                    0x21,                           // Host→接口, 类请求
                    CDC_REQUEST_SET_LINE_CODING,
                    0,                              // wValue=0
                    0,                              // wIndex=接口0 (CDC 控制)
                    &line_coding, sizeof(line_coding));
        ESP_LOGI(TAG, "SET_LINE_CODING: %s", ok ? "OK" : "FAIL");

        // ---- SET_CONTROL_LINE_STATE: DTR=1, RTS=1 ----
        ok = cdc_ctrl_xfer_sync(daddr,
                0x21,
                CDC_REQUEST_SET_CONTROL_LINE_STATE,
                0x0003,                             // wValue: DTR=1, RTS=1
                0,                                  // wIndex=接口0
                NULL, 0);
        ESP_LOGI(TAG, "DTR/RTS 激活: %s", ok ? "OK" : "FAIL");

        // ---- 手动打开 Bulk 端点 (CDC 类驱动不存在, 裸端点独裁) ----
        const uint8_t ep_out_addr = 0x01;
        const uint8_t ep_in_addr  = 0x81;

        tusb_desc_endpoint_t ep_out = {
            .bLength          = sizeof(tusb_desc_endpoint_t),
            .bDescriptorType  = TUSB_DESC_ENDPOINT,
            .bEndpointAddress = ep_out_addr,
            .bmAttributes     = { .xfer = TUSB_XFER_BULK },
            .wMaxPacketSize   = 64,
        };
        ESP_LOGI(TAG, "Bulk OUT 0x%02X (MPS=64): %s", ep_out_addr,
                 tuh_edpt_open(daddr, &ep_out) ? "OK" : "FAIL");

        tusb_desc_endpoint_t ep_in = {
            .bLength          = sizeof(tusb_desc_endpoint_t),
            .bDescriptorType  = TUSB_DESC_ENDPOINT,
            .bEndpointAddress = ep_in_addr,
            .bmAttributes     = { .xfer = TUSB_XFER_BULK },
            .wMaxPacketSize   = 64,
        };
        bool in_opened = tuh_edpt_open(daddr, &ep_in);
        ESP_LOGI(TAG, "Bulk IN 0x%02X: %s", ep_in_addr, in_opened ? "OK" : "FAIL");

        s_bulk_out_ep = ep_out_addr;
        s_bulk_in_ep  = ep_in_addr;

        // ---- 启动连续 IN 轮询 ----
        if (in_opened) {
            tuh_xfer_t in_xfer = {
                .daddr       = daddr,
                .ep_addr     = s_bulk_in_ep,
                .buffer      = s_async_rx_buf,
                .buflen      = sizeof(s_async_rx_buf),
                .complete_cb = usb_host_rx_cb,
                .user_data   = 0,
            };
            s_bulk_in_inflight = tuh_edpt_xfer(&in_xfer);
            s_last_rx_cb_time   = now;
            s_last_rx_data_time = now;
            ESP_LOGI(TAG, "Bulk IN 轮询启动: %s",
                     s_bulk_in_inflight ? "OK" : "FAIL");
        }
    }

    // ================================================================
    // 状态日志 (每 5s)
    // ================================================================
    if (now - s_last_log > 5000) {
        s_last_log = now;
        bool port_connected = hcd_port_connect_status(0);
        ESP_LOGI(TAG, "USB 状态: inited=%d mounted=%d port_connected=%d",
                 tuh_inited(), s_cdc_mounted, port_connected);
        ESP_LOGI(TAG, "CDC ringbuf: %zu bytes", cdc_ringbuf_available());
    }
}
