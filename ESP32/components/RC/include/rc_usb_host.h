#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief 初始化 USB Host CDC ACM
 * @note  配置 PHY 为 Host 模式，启动 TinyUSB Host 栈。
 *        初始化成功后 USB 口可连接飞控 CDC ACM 设备。
 */
void usb_host_cdc_init(void);

/**
 * @brief 反初始化 USB Host，释放 PHY
 */
void usb_host_cdc_deinit(void);

/**
 * @brief CDC ACM 设备是否已挂载
 */
bool usb_host_cdc_connected(void);

/**
 * @brief 从接收环形缓冲读取飞控数据
 * @param buf     接收缓冲
 * @param max_len 最大读取字节数
 * @return 实际读取字节数，0 = 无数据
 */
int usb_host_cdc_read(uint8_t *buf, size_t max_len);

/**
 * @brief 向飞控 CDC ACM 设备写入数据
 * @param data 数据
 * @param len  长度
 * @return 实际写入字节数
 */
int usb_host_cdc_write(const uint8_t *data, size_t len);

/**
 * @brief 检查是否有飞控数据待读取
 */
bool usb_host_cdc_available(void);

/**
 * @brief 轮询 USB Host 事件
 * @note  必须定期调用（如每 10ms），驱动 tuh_task()
 */
void usb_host_cdc_poll(void);

/**
 * @brief 重置 USB CDC 状态 (断开、异常恢复统一入口)
 * @note  清空所有锁、缓冲、时间戳 + 总线弹跳触发 PHY 重检
 */
void usb_host_cdc_reset(void);

/**
 * @brief USB 总线弹跳: 拉低 D+/D- 再释放, 触发 PHY 重新检测设备
 * @note  当 port_connected=0 时调用, 约 30ms 阻塞
 */
void usb_host_cdc_bus_bounce(void);

#ifdef __cplusplus
}
#endif
