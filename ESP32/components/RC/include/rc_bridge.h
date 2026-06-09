#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

typedef enum {
    BRIDGE_PATH_CRSF_MSP,  // BLE NUS ↔ CRSF MSP 透穿 (走 ELRS RF 链路)
    BRIDGE_PATH_USB_CDC,   // BLE NUS ↔ USB CDC ACM 透穿 (直插飞控 USB)
} bridge_path_t;

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
