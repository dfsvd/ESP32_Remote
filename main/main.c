
#include <stdlib.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "rc_usb.h"
#include "tinyusb.h"
#include "rc_read.h"
#include "rc_wf.h"
#include "rc_crsf.h"
#include "rc_ble.h"
#include "nvs_flash.h"

static const char *TAG = "FPV_RC";
#define APP_BUTTON (GPIO_NUM_0) // Use BOOT signal by default
// 赋初始合法值，防止未启用的通道发送 0 导致 Windows 丢弃整个 HID 报文
static fpv_joystick_report_t joy = {
    .roll = 1500, .pitch = 1500, .throttle = 1000, .yaw = 1500,
    .aux1 = 1000, .aux2 = 1000, .aux3 = 1000, .aux4 = 1000,
    // 关键点：提前给还没有写逻辑的开关通道垫上 1000
    .sw1 = 1000, .sw2 = 1000, .sw3 = 1000, .sw4 = 1000,
    .sw5 = 1000, .sw6 = 1000, .sw7 = 1000, .sw8 = 1000
};
void app_main(void)
{
    uint8_t ble_mode = 0;
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
  
    load_settings_from_nvs();
    // Initialize button that will trigger HID reports
    const gpio_config_t boot_button_config = {
        .pin_bit_mask = BIT64(APP_BUTTON),
        .mode = GPIO_MODE_INPUT,
        .intr_type = GPIO_INTR_DISABLE,
        .pull_up_en = true,
        .pull_down_en = false,
    };
    
    ESP_ERROR_CHECK(gpio_config(&boot_button_config));
    xTaskCreatePinnedToCore( ADC_TASK, "adc_task", 4096, &joy,  4, NULL, 1 );
    crsf_init(&joy);

    vTaskDelay(pdMS_TO_TICKS(50));


    if ( gpio_get_level(GPIO_NUM_17)==0)
    {
        rc_wifi_server_init(&joy);
    } else if ( gpio_get_level(GPIO_NUM_18)==0)
    {
        ble_init(&joy);
        ble_mode = 1;
    } else {
        usb_init();
    }
    
    
    
    while (1)
    {
        if (ble_mode) 
        {
            ble_update_input(&joy);
        }

        if (tud_mounted())
        {
            app_send_fpv_data(&joy);
        }
        // ESP_LOGI(TAG,"%d ,%d ,%d ,%d, %d ,%d ,%d ,%d ,%d ,%d ,%d ,%d ,%d ,%d, %d, %d ", joy.throttle, joy.yaw, joy.roll, joy.pitch, joy.aux1, joy.aux2, joy.aux3, joy.aux4, joy.sw1, joy.sw2, joy.sw3, joy.sw4, joy.sw5, joy.sw6, joy.sw7, joy.sw8);

        vTaskDelay(1);
    }
}
