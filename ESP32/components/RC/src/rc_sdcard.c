#include "rc_sdcard.h"
#include "rc_sdcard.h"
#include "esp_log.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "driver/sdspi_host.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "SDCARD";
static bool s_mounted = false;
static sdmmc_card_t *s_card = NULL;

esp_err_t sdcard_mount(void) {
    if (s_mounted) return ESP_OK;

    // 配置 SPI 总线
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = SD_SPI_MOSI,
        .miso_io_num = SD_SPI_MISO,
        .sclk_io_num = SD_SPI_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };

    esp_err_t ret = spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI 总线初始化失败: %s", esp_err_to_name(ret));
        return ret;
    }

    // SD 卡 SPI 设备配置
    sdspi_device_config_t slot_cfg = {
        .host_id   = SPI2_HOST,
        .gpio_cs   = SD_SPI_CS,
        .gpio_cd   = SDSPI_SLOT_NO_CD,
        .gpio_wp   = SDSPI_SLOT_NO_WP,
        .gpio_int  = GPIO_NUM_NC,
        .gpio_wp_polarity = 0,
        .duty_cycle_pos = 0,
        .wait_for_miso = 0,
    };

    // 挂载 FATFS
    esp_vfs_fat_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
    };

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    ret = esp_vfs_fat_sdspi_mount(SD_MOUNT_POINT, &host, &slot_cfg, &mount_cfg, &s_card);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "TF 卡挂载失败: %s (可能无卡或未格式化)", esp_err_to_name(ret));
        spi_bus_free(SPI2_HOST);
        return ret;
    }

    s_mounted = true;
    sdmmc_card_print_info(stdout, s_card);
    ESP_LOGI(TAG, "TF 卡已挂载到 %s", SD_MOUNT_POINT);
    return ESP_OK;
}

void sdcard_unmount(void) {
    if (!s_mounted) return;
    esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT, s_card);
    spi_bus_free(SPI2_HOST);
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
