#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include "rc_read.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define BLE_HID_DEVICE_NAME "FPV RC BLE V2"
#define BLE_HOST_TASK_STACK 4096
#define BLE_HID_SEND_TASK_STACK 4096
#define BLE_HID_TASK_PRIORITY 5
#define BLE_HID_TASK_PERIOD_MS 10

typedef enum {
    BLE_MODE_HID,   // BLE HID 游戏手柄 (现有模式)
    BLE_MODE_NUS,   // BLE NUS 串口透穿 (调参用)
} ble_mode_t;

void ble_init(fpv_joystick_report_t *joy, ble_mode_t mode);
void ble_update_input(const fpv_joystick_report_t *joy);
bool ble_is_connected(void);
bool ble_is_paired(void);

// ---- BLE UART (NUS) API ----
bool ble_uart_is_subscribed(void);
int  ble_uart_read(uint8_t *buf, size_t max_len);
void ble_uart_send(const uint8_t *data, size_t len);
bool ble_uart_flush(void);  // 清空 TX 待发送缓冲

// ---- 链路健康 ----
int  ble_get_last_disconnect_reason(void);  // 上次 BLE 断开原因 (0=未断开)
void ble_reset_nus_stream(void);            // 清空 NUS 接收流缓冲

#ifdef __cplusplus
}
#endif
