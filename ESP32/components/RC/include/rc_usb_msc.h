#pragma once
#include "sdmmc_cmd.h"

/**
 * @brief 初始化 USB MSC (U盘模式)
 * @param card 已挂载的 SD 卡指针
 */
void usb_msc_init(sdmmc_card_t *card);
