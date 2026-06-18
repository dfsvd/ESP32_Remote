#include "rc_sdcard.h"
#include "esp_log.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tinyusb_msc.h"

static const char *TAG = "USB_MSC";

void usb_msc_init(void) {
    ESP_LOGI(TAG, "初始化 USB MSC (TF卡 → U盘)...");

    // 1. 通过 rc_sdcard 模块挂载 SD 卡 (复用 SDMMC 初始化, 避免重复)
    esp_err_t ret = sdcard_mount();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SD 卡挂载失败, MSC 无法启动");
        return;
    }

    sdmmc_card_t *card = sdcard_get_card();
    if (!card) {
        ESP_LOGE(TAG, "无法获取 SD 卡指针");
        return;
    }

    // 2. 安装 TinyUSB 驱动
    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG();
    tusb_cfg.descriptor.device = NULL;
    tusb_cfg.descriptor.full_speed_config = NULL;
    tusb_cfg.descriptor.string = NULL;
    tusb_cfg.descriptor.string_count = 0;

    ret = tinyusb_driver_install(&tusb_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "TinyUSB 驱动安装失败: %s", esp_err_to_name(ret));
        return;
    }

    // 3. 安装 MSC 驱动
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

    // 4. 创建 SDMMC 存储并暴露给 USB
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
