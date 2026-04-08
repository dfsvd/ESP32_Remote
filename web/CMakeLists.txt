idf_component_register(
    SRCS "src/rc_usb.c" "src/rc_read.c"
    INCLUDE_DIRS "include"
    # 添加底层 tinyusb、driver (GPIO需要) 和 log 的依赖
    # PRIV_REQUIRES esp_tinyusb tinyusb driver log 
    REQUIRES espressif__esp_tinyusb esp_adc 
)