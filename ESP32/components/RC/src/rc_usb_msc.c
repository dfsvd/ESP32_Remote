#include "rc_sdcard.h"
#include "esp_log.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tinyusb_msc.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"

static const char *TAG = "USB_MSC";

void usb_msc_init(void) {
    ESP_LOGI(TAG, "初始化 USB MSC (TF卡 → U盘, SDMMC 1-bit)...");

    // 1. 初始化 SDMMC 宿主机 + 卡槽
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = SDMMC_HOST_SLOT_1;

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
        .width = 1,
        .flags = SDMMC_SLOT_FLAG_INTERNAL_PULLUP,
    };

    ESP_ERROR_CHECK(sdmmc_host_init());
    ESP_ERROR_CHECK(sdmmc_host_init_slot(SDMMC_HOST_SLOT_1, &slot_config));

    // 2. 初始化 SD 卡 (不挂载 FATFS, 直接暴露给 USB)
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
