#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "class/hid/hid_device.h"
#include "class/vendor/vendor_device.h" // 必须引入 Vendor 类支持 Xbox
#include "driver/gpio.h"
#include "rc_read.h"

// ===== 模拟器模式枚举 =====
typedef enum {
    SIM_MODE_DEFAULT = 0,   // 标准 HID FPV 模式 (普通模拟器)
    SIM_MODE_XBOX           // Xbox 360 手柄模式 (大疆虚拟飞行专版)
} sim_mode_t;

typedef struct {
    uint16_t ch1;
    uint16_t ch2;
    uint16_t ch3;
    uint16_t ch4;
    uint16_t ch5;
    uint16_t ch6;
    uint16_t ch7;
    uint16_t ch8;
    uint16_t ch9;
    uint16_t ch10;
    uint16_t ch11;
    uint16_t ch12;
    uint16_t ch13;
    uint16_t ch14;
    uint16_t ch15;
    uint16_t ch16;
} fpv_usb_report_t;

typedef struct __attribute__((packed)) {
    int16_t x;
    int16_t y;
    int16_t z;
    int16_t rz;
    uint16_t buttons;
} fpv_hid_axes_report_t;

// ===== Xbox 360 手柄标准 20 字节数据包 =====
typedef struct __attribute__((packed)) {
    uint8_t  type;       // 固定为 0x00
    uint8_t  size;       // 固定为 0x14 (20字节)
    uint16_t buttons;    // 16位按键掩码
    uint8_t  lt;         // 左扳机
    uint8_t  rt;         // 右扳机
    int16_t  lx;         // 左摇杆 X : -32768 ~ 32767
    int16_t  ly;         // 左摇杆 Y : -32768 ~ 32767
    int16_t  rx;         // 右摇杆 X : -32768 ~ 32767
    int16_t  ry;         // 右摇杆 Y : -32768 ~ 32767
    uint8_t  reserved[6];// 保留位，固定为 0
} xinput_report_t;

#define TUSB_DESC_TOTAL_LEN (TUD_CONFIG_DESC_LEN + CFG_TUD_HID * TUD_HID_DESC_LEN)
extern sim_mode_t current_sim_mode;

void build_usb_channel_report(const fpv_joystick_report_t *joy, fpv_usb_report_t *report);
int16_t map_axis_centered(uint16_t value, bool invert);
uint8_t map_axis_trigger(uint16_t value);
void build_hid_axes_report(const fpv_usb_report_t *usb_report, fpv_hid_axes_report_t *report);
void app_send_fpv_data(fpv_joystick_report_t *joy);
void usb_init(void);
void usb_init_mode(sim_mode_t mode);

#ifdef __cplusplus
}
#endif