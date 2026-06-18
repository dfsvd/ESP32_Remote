#include "rc_sdcard.h"
#include "esp_log.h"
#include "driver/sdmmc_host.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "SDCARD";

/* WARNING: 模块非线程安全。当前设计假定 SD 卡在系统生命周期内常驻，
 * 仅启动时 Mount 一次，永不主动卸载。不支持多任务下的并发卸载/热插拔 */

static bool s_mounted = false;
static sdmmc_card_t *s_card = NULL;

/* 全量读入内存的硬上限: 超过此值请改用 sdcard_fopen 流式读取 */
#define SDCARD_MAX_MALLOC_SIZE (64 * 1024)

esp_err_t sdcard_mount(void) {
    if (s_mounted) return ESP_OK;

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = SDMMC_HOST_SLOT_1;

    sdmmc_slot_config_t slot_config = {
        .clk   = SDMMC_CLK,
        .cmd   = SDMMC_CMD,
        .d0    = SDMMC_D0,
        .d1    = GPIO_NUM_NC,
        .d2    = GPIO_NUM_NC,
        .d3    = GPIO_NUM_NC,
        .d4    = GPIO_NUM_NC,
        .d5    = GPIO_NUM_NC,
        .d6    = GPIO_NUM_NC,
        .d7    = GPIO_NUM_NC,
        .gpio_cd = GPIO_NUM_NC,
        .gpio_wp = GPIO_NUM_NC,
        .width = 1,
        .flags = SDMMC_SLOT_FLAG_INTERNAL_PULLUP,
    };

    esp_vfs_fat_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
    };

    // 一站式: 主机初始化 + 卡槽初始化 + 卡协商 + FATFS 挂载
    esp_err_t ret = esp_vfs_fat_sdmmc_mount(SD_MOUNT_POINT, &host,
                                            &slot_config, &mount_cfg, &s_card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "TF 卡挂载失败: %s", esp_err_to_name(ret));
        return ret;
    }

    s_mounted = true;
    sdmmc_card_print_info(stdout, s_card);
    ESP_LOGI(TAG, "TF 卡已挂载到 %s (SDMMC 1-bit)", SD_MOUNT_POINT);
    return ESP_OK;
}

void sdcard_unmount(void) {
    if (!s_mounted) return;
    esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT, s_card);
    sdmmc_host_deinit();
    s_mounted = false;
    s_card = NULL;
    ESP_LOGI(TAG, "TF 卡已卸载");
}

bool sdcard_is_mounted(void) {
    return s_mounted;
}

FILE *sdcard_fopen(const char *path) {
    if (!s_mounted || !path) return NULL;

    char full_path[128];
    int n = snprintf(full_path, sizeof(full_path), "%s%s", SD_MOUNT_POINT, path);
    if (n >= (int)sizeof(full_path)) return NULL;

    return fopen(full_path, "rb");
}

esp_err_t sdcard_read_file(const char *path, uint8_t **buf, size_t *size) {
    if (!s_mounted || !path || !buf || !size) return ESP_ERR_INVALID_ARG;

    FILE *f = sdcard_fopen(path);
    if (!f) return ESP_ERR_NOT_FOUND;

    fseek(f, 0, SEEK_END);
    size_t file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    // 防御性编程: 超过 64KB 拒绝全量读入, 请改用 sdcard_fopen 流式读取
    if (file_size > SDCARD_MAX_MALLOC_SIZE) {
        fclose(f);
        ESP_LOGE(TAG, "文件过大 (%zu bytes)，拒绝全量载入内存", file_size);
        return ESP_ERR_NO_MEM;
    }

    uint8_t *data = malloc(file_size);
    if (!data) { fclose(f); return ESP_ERR_NO_MEM; }

    size_t read = fread(data, 1, file_size, f);
    fclose(f);
    if (read != file_size) { free(data); return ESP_ERR_INVALID_SIZE; }

    *buf = data;
    *size = file_size;
    return ESP_OK;
}

sdmmc_card_t *sdcard_get_card(void) {
    return s_card;
}

bool sdcard_file_exists(const char *path) {
    FILE *f = sdcard_fopen(path);
    if (!f) return false;
    fclose(f);
    return true;
}
