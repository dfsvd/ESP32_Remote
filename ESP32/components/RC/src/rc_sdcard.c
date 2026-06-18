#include "rc_sdcard.h"
#include "esp_log.h"
#include "driver/sdmmc_host.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "SDCARD";
static bool s_mounted = false;
static sdmmc_card_t *s_card = NULL;

esp_err_t sdcard_mount(void) {
    if (s_mounted) return ESP_OK;

    // SDMMC 宿主机配置 (Slot 1 = GPIO 矩阵, 引脚可自定义)
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = SDMMC_HOST_SLOT_1;

    // 引脚配置并通过 GPIO 矩阵映射到自定义引脚
    sdmmc_slot_config_t slot_config = {
        .clk   = SDMMC_CLK,      // 47
        .cmd   = SDMMC_CMD,      // 48
        .d0    = SDMMC_D0,       // 21
        .d1    = GPIO_NUM_NC,
        .d2    = GPIO_NUM_NC,
        .d3    = GPIO_NUM_NC,
        .d4    = GPIO_NUM_NC,
        .d5    = GPIO_NUM_NC,
        .d6    = GPIO_NUM_NC,
        .d7    = GPIO_NUM_NC,
        .gpio_cd = GPIO_NUM_NC,
        .gpio_wp = GPIO_NUM_NC,
        .width = 1,                     // 1-bit mode
        .flags = SDMMC_SLOT_FLAG_INTERNAL_PULLUP,
    };

    // 初始化 SDMMC 外设
    esp_err_t ret = sdmmc_host_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SDMMC 宿主机初始化失败: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = sdmmc_host_init_slot(SDMMC_HOST_SLOT_1, &slot_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SDMMC 卡槽初始化失败: %s", esp_err_to_name(ret));
        sdmmc_host_deinit();
        return ret;
    }

    // 挂载 FATFS
    esp_vfs_fat_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
    };

    ret = esp_vfs_fat_sdmmc_mount(SD_MOUNT_POINT, &host, &slot_config, &mount_cfg, &s_card);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "TF 卡挂载失败: %s (可能无卡或未格式化)", esp_err_to_name(ret));
        sdmmc_host_deinit();
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

esp_err_t sdcard_read_file(const char *path, uint8_t **buf, size_t *size) {
    if (!s_mounted || !path || !buf || !size) return ESP_ERR_INVALID_ARG;

    char full_path[128];
    int n = snprintf(full_path, sizeof(full_path), "%s%s", SD_MOUNT_POINT, path);
    if (n >= (int)sizeof(full_path)) return ESP_ERR_INVALID_SIZE;

    FILE *f = fopen(full_path, "rb");
    if (!f) return ESP_ERR_NOT_FOUND;

    fseek(f, 0, SEEK_END);
    size_t file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

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
    if (!s_mounted || !path) return false;
    char full_path[128];
    int n = snprintf(full_path, sizeof(full_path), "%s%s", SD_MOUNT_POINT, path);
    if (n >= (int)sizeof(full_path)) return false;
    FILE *f = fopen(full_path, "rb");
    if (!f) return false;
    fclose(f);
    return true;
}
