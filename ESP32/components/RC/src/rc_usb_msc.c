#include "rc_sdcard.h"
#include "esp_log.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tinyusb_msc.h"
#include "sdmmc_cmd.h"
#include "driver/sdspi_host.h"
#include "driver/spi_master.h"

static const char *TAG = "USB_MSC";

void usb_msc_init(void) {
    ESP_LOGI(TAG, "初始化 USB MSC (TF卡 → U盘)...");

    // 1. 初始化 SPI 总线
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = SD_SPI_MOSI,
        .miso_io_num = SD_SPI_MISO,
        .sclk_io_num = SD_SPI_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO));

    // 2. 初始化 SD 卡 (不挂载 FATFS)
    sdspi_device_config_t slot_cfg = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_cfg.host_id = SPI2_HOST;
    slot_cfg.gpio_cs = SD_SPI_CS;
    slot_cfg.gpio_cd = SDSPI_SLOT_NO_CD;
    slot_cfg.gpio_wp = SDSPI_SLOT_NO_WP;

    sdspi_dev_handle_t sd_handle;
    ESP_ERROR_CHECK(sdspi_host_init_device(&slot_cfg, &sd_handle));

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = sd_handle;

    sdmmc_card_t *card = (sdmmc_card_t *)malloc(sizeof(sdmmc_card_t));
    ESP_ERROR_CHECK(sdmmc_card_init(&host, card));
    sdmmc_card_print_info(stdout, card);

    // 3. 安装 TinyUSB 驱动
    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG();
    tusb_cfg.descriptor.device = NULL;
    tusb_cfg.descriptor.full_speed_config = NULL;
    tusb_cfg.descriptor.string = NULL;
    tusb_cfg.descriptor.string_count = 0;

    esp_err_t ret = tinyusb_driver_install(&tusb_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "TinyUSB 驱动安装失败: %s", esp_err_to_name(ret));
        return;
    }

    // 4. 安装 MSC 驱动
    tinyusb_msc_driver_config_t driver_cfg = {
        .user_flags = { .auto_mount_off = 0 },
        .callback = NULL,
        .callback_arg = NULL,
    };
    ret = tinyusb_msc_install_driver(&driver_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "MSC 驱动安装失败: %s", esp_err_to_name(ret));
        return;
    }

    // 5. 创建 SDMMC 存储并暴露给 USB
    tinyusb_msc_storage_config_t msc_cfg = {
        .medium.card = card,
        .mount_point = TINYUSB_MSC_STORAGE_MOUNT_USB,
    };
    tinyusb_msc_storage_handle_t handle;
    ret = tinyusb_msc_new_storage_sdmmc(&msc_cfg, &handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "MSC 存储创建失败: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "USB MSC 就绪! 请用 USB 线连接电脑 OTG 口");
    ESP_LOGI(TAG, "========================================");
}
