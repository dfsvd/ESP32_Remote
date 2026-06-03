# 收集音频文件嵌入固件
file(GLOB RC_AUDIO_FILES ${CMAKE_CURRENT_LIST_DIR}/../../audio/*.wav)

idf_component_register(
    SRCS "src/rc_led.c" "src/rc_usb.c" "src/rc_read.c" "src/rc_wf.c" "src/rc_crsf.c" "src/rc_ble.c" "src/rc_audio.c"
    INCLUDE_DIRS "include"
    EMBED_TXTFILES "src/index.html"
    EMBED_FILES ${RC_AUDIO_FILES}
    REQUIRES espressif__esp_tinyusb esp_adc esp_wifi esp_http_server nvs_flash esp_netif esp_event esp_driver_gpio esp_driver_uart esp_driver_i2s bt led_strip
)
