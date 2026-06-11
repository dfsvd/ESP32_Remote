#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

typedef enum {
    BRIDGE_PATH_CRSF_MSP,  // BLE NUS ↔ CRSF MSP 透穿 (走 ELRS RF 链路)
    BRIDGE_PATH_USB_CDC,   // BLE NUS ↔ USB CDC ACM 透穿 (直插飞控 USB)
    BRIDGE_PATH_UART,      // BLE NUS ↔ 纯串口透穿 (直连飞控 UART)
} bridge_path_t;

// UART 桥接参数（可在此统一修改）
#define BRIDGE_UART_PORT      UART_NUM_1
#define BRIDGE_UART_TX_PIN    GPIO_NUM_17
#define BRIDGE_UART_RX_PIN    GPIO_NUM_18
#define BRIDGE_UART_BAUD      115200
#define BRIDGE_UART_BUF_SIZE  2048  // RX 环形缓冲大小

/**
 * @brief 启动 BLE NUS ↔ (CRSF MSP / USB CDC ACM) 桥接任务
 * @param path 桥接路径
 */
void bridge_start(bridge_path_t path);

/**
 * @brief 停止桥接任务
 */
void bridge_stop(void);

/**
 * @brief 桥接任务是否正在运行
 */
bool bridge_is_running(void);

#ifdef __cplusplus
}
#endif
