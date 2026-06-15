#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

// ========== SPI 引脚配置 (正点原子 ESP32-S3 开发板) ==========
#define SD_SPI_MOSI    GPIO_NUM_11
#define SD_SPI_MISO    GPIO_NUM_13
#define SD_SPI_SCLK    GPIO_NUM_12
#define SD_SPI_CS      GPIO_NUM_2

// SD 卡挂载点
#define SD_MOUNT_POINT "/sd"

/**
 * @brief 挂载 TF 卡 (SPI + FATFS)
 * @return ESP_OK 成功, 其他失败(无卡/未格式化)
 */
esp_err_t sdcard_mount(void);

/**
 * @brief 卸载 TF 卡
 */
void sdcard_unmount(void);

/**
 * @brief 判断 SD 卡是否已挂载
 */
bool sdcard_is_mounted(void);

/**
 * @brief 从 SD 卡读取文件
 * @param path  文件路径 (相对于挂载点, 如 "/tiles/12/3344/1784.png")
 * @param buf   输出缓冲区 (需 caller 用 free() 释放)
 * @param size  输出文件大小
 * @return ESP_OK 成功
 */
esp_err_t sdcard_read_file(const char *path, uint8_t **buf, size_t *size);

/**
 * @brief 判断文件是否存在
 */
bool sdcard_file_exists(const char *path);
