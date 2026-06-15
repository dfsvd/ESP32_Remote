# 收集音频文件嵌入固件
file(GLOB RC_AUDIO_FILES ${CMAKE_CURRENT_LIST_DIR}/../../audio/*.wav)

# TinyUSB managed component 路径
set(tinyusb_src_dir "${CMAKE_CURRENT_LIST_DIR}/../../managed_components/espressif__tinyusb")

idf_component_register(
    SRCS "src/rc_led.c" "src/rc_usb.c" "src/rc_read.c" "src/rc_wf.c" "src/rc_crsf.c" "src/rc_ble.c" "src/rc_bridge.c" "src/rc_usb_host.c"
         "src/rc_audio.c" "src/tile_data.c" "src/rc_sdcard.c"
         # TinyUSB Host 栈源码 (CDC 类驱动已禁用, 使用裸端点)
         "${tinyusb_src_dir}/src/host/usbh.c"
         "${tinyusb_src_dir}/src/portable/synopsys/dwc2/hcd_dwc2.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "${tinyusb_src_dir}/src"
    EMBED_TXTFILES "src/index.html"
    EMBED_FILES ${RC_AUDIO_FILES}
    REQUIRES espressif__esp_tinyusb esp_adc esp_wifi esp_http_server nvs_flash esp_netif esp_event esp_driver_gpio esp_driver_uart esp_driver_i2s bt led_strip usb
)

# TinyUSB Host 编译定义 (CDC 类驱动已禁用, 裸端点独裁, 彻底消除 DATA Toggle 冲突)
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    CFG_TUH_ENABLED=1
    CFG_TUH_CDC=0
    CFG_TUH_API_EDPT_XFER=1
)

# hcd_dwc2.c 需要中断处理函数声明 (CFG_TUD_ENABLED=1 时 dwc2_esp32.h 需要 dcd_int_handler)，通过 dwc2_forward.h 提供
set_source_files_properties(
    "${tinyusb_src_dir}/src/portable/synopsys/dwc2/hcd_dwc2.c"
    PROPERTIES
    COMPILE_OPTIONS "-include;dwc2_forward.h"
)
