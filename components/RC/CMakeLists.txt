idf_component_register(
    SRCS "src/rc_usb.c" "src/rc_read.c" "src/rc_wf.c" "src/rc_crsf.c" "src/rc_ble.c"
    INCLUDE_DIRS "include"
    EMBED_TXTFILES "src/index.html"
    REQUIRES espressif__esp_tinyusb esp_adc esp_wifi esp_http_server nvs_flash esp_netif esp_event esp_driver_gpio esp_driver_uart bt
)
