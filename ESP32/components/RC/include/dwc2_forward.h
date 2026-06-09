// 为 DWC2 中断处理函数提供前向声明
// 当 CFG_TUD_ENABLED 和 CFG_TUH_ENABLED 同时启用时，
// dwc2_esp32.h 中的 dwc2_int_handler_wrap 需要两种声明
#pragma once
#include <stdint.h>
#include <stdbool.h>

void dcd_int_handler(uint8_t rhport);
void hcd_int_handler(uint8_t rhport, bool in_isr);
