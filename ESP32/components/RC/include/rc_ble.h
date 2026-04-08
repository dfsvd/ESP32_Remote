#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include "rc_read.h"

#define BLE_HID_DEVICE_NAME      "FPV RC BLE V2"
#define BLE_HID_HOST_TASK_STACK  4096
#define BLE_HID_SEND_TASK_STACK  4096
#define BLE_HID_TASK_PRIORITY    5
#define BLE_HID_TASK_PERIOD_MS   10

void ble_init(fpv_joystick_report_t *joy);
void ble_update_input(const fpv_joystick_report_t *joy);
bool ble_is_connected(void);
bool ble_is_paired(void);

#ifdef __cplusplus
}
#endif
