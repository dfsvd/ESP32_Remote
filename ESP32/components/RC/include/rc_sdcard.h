#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "sdmmc_cmd.h"

// ========== SDMMC 1-bit 引脚配置 (ESP32-S3 GPIO 矩阵) ==========
#define SDMMC_CLK    GPIO_NUM_47   // SD_CLK  (SPICLK_P)
#define SDMMC_CMD    GPIO_NUM_48   // SD_CMD  (SPICLK_N)
#define SDMMC_D0     GPIO_NUM_21   // SD_DAT0

// SD 卡挂载点
#define SD_MOUNT_POINT "/sd"

/**
 * @brief 挂载 TF 卡 (SDMMC 1-bit + FATFS)
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
 * @brief 打开文件 (流式读取用, 返回 FILE* 指针)
 * @param path  文件路径 (相对于挂载点, 如 "/tiles/12/3344/1784.png")
 * @return FILE* 指针, NULL 表示失败
 * @note 调用方用 fread/fclose 操作, 不经过 malloc
 */
FILE *sdcard_fopen(const char *path);

/**
 * @brief 从 SD 卡读取文件 (一次性读入内存)
 * @param path  文件路径 (相对于挂载点, 如 "/tiles/12/3344/1784.png")
 * @param buf   输出缓冲区 (需 caller 用 free() 释放)
 * @param size  输出文件大小
 * @return ESP_OK 成功
 * @note 仅在需要完整文件内容时使用, 否则优先用 sdcard_fopen 流式读取
 */
esp_err_t sdcard_read_file(const char *path, uint8_t **buf, size_t *size);

/**
 * @brief 判断文件是否存在
 */
bool sdcard_file_exists(const char *path);

/**
 * @brief 获取 SD 卡指针 (用于 MSC)
 */
sdmmc_card_t *sdcard_get_card(void);
