#include "rc_sdcard.h"
#include "esp_log.h"
#include "tinyusb.h"
#include "tinyusb_msc.h"

static const char *TAG = "USB_MSC";

void usb_msc_init(sdmmc_card_t *card) {
    ESP_LOGI(TAG, "初始化 USB MSC (TF卡 → U盘)...");

    if (!card) {
        ESP_LOGE(TAG, "SD 卡指针为空");
        return;
    }

    // 先卸载 FATFS (MSC 需要直接访问块设备)
    // 注意: sdcard_unmount 会释放 SPI 总线
    // 但 MSC 也需要 SPI 总线, 所以我们不卸载, 而是让库重新挂载

    // 安装 MSC 驱动
    tinyusb_msc_driver_config_t driver_cfg = {
        .user_flags = { .auto_mount_off = 0 },
        .callback = NULL,
        .callback_arg = NULL,
    };

    esp_err_t ret = tinyusb_msc_install_driver(&driver_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "MSC 驱动安装失败: %s", esp_err_to_name(ret));
        return;
    }

    // 创建 SDMMC 存储
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

    ESP_LOGI(TAG, "USB MSC 就绪! 请用 USB 线连接电脑");
    ESP_LOGI(TAG, "电脑上会出现一个可移动磁盘 (TF 卡)");
}
