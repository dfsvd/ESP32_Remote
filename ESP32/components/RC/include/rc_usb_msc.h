#pragma once

/**
 * @brief 初始化 USB MSC (U盘模式)
 * 直接在内部初始化 SD 卡并暴露为 USB 存储设备
 */
void usb_msc_init(void);
