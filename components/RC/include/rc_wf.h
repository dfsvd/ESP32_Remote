#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include "rc_read.h"



// 启动 WiFi (AP模式) 和 WebSocket 服务器
// 传入 joy 结构体指针，以便在后台任务中读取并广播通道值
void rc_wifi_server_init(fpv_joystick_report_t *joy);
void load_settings_from_nvs(void);
void save_settings_to_nvs(void);
#ifdef __cplusplus
}
#endif