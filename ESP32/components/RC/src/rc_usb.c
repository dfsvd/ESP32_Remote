#include "rc_usb.h"
#include "class/vendor/vendor_device.h" // 引入 Vendor 类头文件

static const char *TAG = "FPV_XBOX";

// 当前模拟器模式，默认为 Xbox 模式
sim_mode_t current_sim_mode = SIM_MODE_XBOX;

void build_usb_channel_report(const fpv_joystick_report_t *joy, fpv_usb_report_t *report)
{
    report->ch1  = joy->roll;
    report->ch2  = joy->pitch;
    report->ch3  = joy->throttle;
    report->ch4  = joy->yaw;
    report->ch5  = joy->aux1;     // SA
    report->ch6  = joy->aux2;     // SB
    report->ch7  = joy->aux3;     // SC
    report->ch8  = joy->aux4;     // SD
    report->ch9  = 1500;
    report->ch10 = 1500;
    report->ch11 = 1500;
    report->ch12 = 1500;
    report->ch13 = 1500;
    report->ch14 = 1500;
    report->ch15 = 1500;
    report->ch16 = 1500;
}

int16_t map_axis_centered(uint16_t value, bool invert)
{
    int32_t mapped = (int32_t)(value - 1500) * 32767 / 500;
    if (mapped > 32767)
    {
        mapped = 32767;
    }
    if (mapped < -32768)
    {
        mapped = -32768;
    }
    return invert ? (int16_t)(-mapped) : (int16_t)mapped;
}

uint8_t map_axis_trigger(uint16_t value)
{
    if (value <= 1000)
    {
        return 0;
    }
    if (value >= 2000)
    {
        return 255;
    }
    return (uint8_t)(((uint32_t)(value - 1000) * 255U) / 1000U);
}

void build_hid_axes_report(const fpv_usb_report_t *usb_report, fpv_hid_axes_report_t *report)
{
    report->x       = map_axis_centered(usb_report->ch1, false);
    report->y       = map_axis_centered(usb_report->ch2, true);
    report->z       = map_axis_centered(usb_report->ch3, true);
    report->rz      = map_axis_centered(usb_report->ch4, false);
    report->buttons = 0;

    if (usb_report->ch5 > 1500)
        report->buttons |= (1 << 0);
    if (usb_report->ch6 > 1500)
        report->buttons |= (1 << 1);
    if (usb_report->ch7 > 1500)
        report->buttons |= (1 << 2);
    if (usb_report->ch8 > 1500)
        report->buttons |= (1 << 3);
    if (usb_report->ch9 > 1500)
        report->buttons |= (1 << 4);
    if (usb_report->ch10 > 1500)
        report->buttons |= (1 << 5);
    if (usb_report->ch11 > 1500)
        report->buttons |= (1 << 6);
    if (usb_report->ch12 > 1500)
        report->buttons |= (1 << 7);
}

// =======================================================================
// [第一部分] 标准 HID FPV 模式描述符 (8轴 + 24按键)
// =======================================================================
const uint8_t hid_report_descriptor_default[] = {
    HID_USAGE_PAGE(HID_USAGE_PAGE_DESKTOP), HID_USAGE(HID_USAGE_DESKTOP_JOYSTICK),
    HID_COLLECTION(HID_COLLECTION_APPLICATION),
    HID_REPORT_ID(1)

    // 8 轴: 按 Usage ID 排序 (0x30~0x37)
    HID_USAGE_PAGE(HID_USAGE_PAGE_DESKTOP),
    HID_USAGE(HID_USAGE_DESKTOP_X),      // CH1: Roll
    HID_USAGE(HID_USAGE_DESKTOP_Y),      // CH2: Pitch
    HID_USAGE(HID_USAGE_DESKTOP_Z),      // CH3: Throttle
    HID_USAGE(HID_USAGE_DESKTOP_RX),     // CH4: Yaw
    HID_USAGE(HID_USAGE_DESKTOP_RY),     // CH5: SA
    HID_USAGE(HID_USAGE_DESKTOP_RZ),     // CH6: SB
    HID_USAGE(HID_USAGE_DESKTOP_SLIDER), // CH7: SC
    HID_USAGE(HID_USAGE_DESKTOP_DIAL),   // CH8: SD
    HID_LOGICAL_MIN_N(1000, 2), HID_LOGICAL_MAX_N(2000, 2), HID_REPORT_COUNT(8),
    HID_REPORT_SIZE(16), HID_INPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE),

    // 24 按键 (1bit, 默认全 0)
    HID_USAGE_PAGE(HID_USAGE_PAGE_BUTTON),
    HID_USAGE_MIN(1), HID_USAGE_MAX(24),
    HID_LOGICAL_MIN(0), HID_LOGICAL_MAX(1),
    HID_REPORT_SIZE(1), HID_REPORT_COUNT(24),
    HID_INPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE),

    HID_COLLECTION_END};

static const uint8_t hid_configuration_descriptor_default[] = {
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, TUSB_DESC_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    TUD_HID_DESCRIPTOR(0, 4, false, sizeof(hid_report_descriptor_default), 0x81, 64, 5),
};
const char *hid_string_descriptor[] = {
    (char[]){0x09, 0x04}, "DARWINFPV", "FPV Simulator Joystick", "000001", "FPV HID interface",
};

// =======================================================================
// [第二部分] XBox 360 模式描述符
// =======================================================================
static const tusb_desc_device_t xinput_device_descriptor = {.bLength         = 18,
                                                            .bDescriptorType = TUSB_DESC_DEVICE,
                                                            .bcdUSB          = 0x0200,
                                                            .bDeviceClass = 0xFF, // Vendor Specific
                                                            .bDeviceSubClass = 0xFF,
                                                            .bDeviceProtocol = 0xFF,
                                                            .bMaxPacketSize0 =
                                                                64, // 【必须是 64】ESP32 硬件要求
                                                            .idVendor      = 0x045E, // 微软 VID
                                                            .idProduct     = 0x028E, // Xbox 360 PID
                                                            .bcdDevice     = 0x0114,
                                                            .iManufacturer = 1,
                                                            .iProduct      = 2,
                                                            .iSerialNumber = 3,
                                                            .bNumConfigurations = 1};

static const uint8_t xinput_configuration_descriptor[] = {
    // Config Descriptor (0x31 = 49 bytes)
    0x09, 0x02, 0x31, 0x00, 0x01, 0x01, 0x00, 0xA0, 0xFA,
    // Interface Descriptor
    0x09, 0x04, 0x00, 0x00, 0x02, 0xFF, 0x5D, 0x01, 0x00,
    // 【核心暗号】Unknown Descriptor 0x21
    0x11, 0x21, 0x00, 0x01, 0x01, 0x25, 0x81, 0x14, 0x00, 0x00, 0x00, 0x00, 0x13, 0x01, 0x08, 0x00,
    0x00,
    // Endpoint IN 1
    0x07, 0x05, 0x81, 0x03, 0x20, 0x00, 0x04,
    // Endpoint OUT 1
    0x07, 0x05, 0x01, 0x03, 0x20, 0x00, 0x08};

const char *xinput_string_descriptor[] = {(char[]){0x09, 0x04}, "Microsoft Corporation",
                                          "Controller", "08FEC93"};

// =======================================================================
// [第三部分] TinyUSB 回调
// =======================================================================

// HID 报告描述符回调
uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
    return hid_report_descriptor_default;
}
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type,
                               uint8_t *buffer, uint16_t reqlen)
{
    return 0;
}
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type,
                           uint8_t const *buffer, uint16_t bufsize)
{
}

// Vendor 回调 (Xbox 安全验证)
bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage,
                                tusb_control_request_t const *request)
{
    if (stage != CONTROL_STAGE_SETUP)
        return true;

    if (request->bmRequestType_bit.type == TUSB_REQ_TYPE_VENDOR)
    {
        if (request->bmRequestType_bit.direction == TUSB_DIR_IN)
        {
            static uint8_t buf[256] = {0};
            return tud_control_xfer(rhport, request, buf, request->wLength);
        }
    }
    return false;
}
// =======================================================================
// [第四部分] 数据发送
// =======================================================================

void app_send_fpv_data(fpv_joystick_report_t *joy)
{
    if (!tud_mounted())
        return;

    fpv_usb_report_t usb_report;
    build_usb_channel_report(joy, &usb_report);

    if (current_sim_mode == SIM_MODE_DEFAULT)
    {
        if (tud_hid_ready())
        {
            usb_hid_report_t hid_report;
            memset(&hid_report, 0, sizeof(hid_report));
            hid_report.ch1 = usb_report.ch1;
            hid_report.ch2 = usb_report.ch2;
            hid_report.ch3 = usb_report.ch3;
            hid_report.ch4 = usb_report.ch4;
            hid_report.ch5 = usb_report.ch5;
            hid_report.ch6 = usb_report.ch6;
            hid_report.ch7 = usb_report.ch7;
            hid_report.ch8 = usb_report.ch8;

            tud_hid_report(1, &hid_report, sizeof(hid_report));

            static TickType_t last_trace_tick = 0;
            TickType_t now = xTaskGetTickCount();
            if ((now - last_trace_tick) >= pdMS_TO_TICKS(500)) {
                last_trace_tick = now;
                ESP_LOGI(TAG,
                    "USB HID X(Roll)=%u Y(Pitch)=%u Z(Thr)=%u Rx(Yaw)=%u "
                    "Ry(SA)=%u Rz(SB)=%u Slider(SC)=%u Dial(SD)=%u B=0x%02X",
                    hid_report.ch1, hid_report.ch2, hid_report.ch3, hid_report.ch4,
                    hid_report.ch5, hid_report.ch6, hid_report.ch7, hid_report.ch8,
                    hid_report.buttons);
            }
        }
    }
    else if (current_sim_mode == SIM_MODE_XBOX)
    {
        xinput_report_t report = {.type = 0x00, .size = 0x14, .buttons = 0, .lt = 0, .rt = 0};
        memset(report.reserved, 0, sizeof(report.reserved));

        report.lx = map_axis_centered(usb_report.ch3, false);
        report.ly = map_axis_centered(usb_report.ch4, false);
        report.ry = map_axis_centered(usb_report.ch1, false);
        report.rx = map_axis_centered(usb_report.ch2, false);

        report.lt = 0;
        report.rt = 0;

        if (usb_report.ch5 > 1500)
            report.buttons |= (1 << 12); // A
        if (usb_report.ch6 > 1500)
            report.buttons |= (1 << 13); // B
        if (usb_report.ch7 > 1500)
            report.buttons |= (1 << 14); // X
        if (usb_report.ch8 > 1500)
            report.buttons |= (1 << 15); // Y
        if (usb_report.ch9 > 1500)
            report.buttons |= (1 << 8);  // LB
        if (usb_report.ch10 > 1500)
            report.buttons |= (1 << 9);  // RB
        if (usb_report.ch11 > 1500)
            report.buttons |= (1 << 5);  // BACK
        if (usb_report.ch12 > 1500)
            report.buttons |= (1 << 4);  // START

        tud_vendor_n_write(0, (uint8_t *)&report, sizeof(report));
        tud_vendor_n_flush(0);
    }
}

// =======================================================================
// [第五部分] 初始化
// =======================================================================

void usb_init_mode(sim_mode_t mode)
{
    current_sim_mode = mode;
    ESP_LOGI(TAG, "USB init mode: %d", mode);

    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG();

    if (mode == SIM_MODE_XBOX)
    {
        tusb_cfg.descriptor.device            = &xinput_device_descriptor;
        tusb_cfg.descriptor.full_speed_config = xinput_configuration_descriptor;
        tusb_cfg.descriptor.string            = xinput_string_descriptor;
        tusb_cfg.descriptor.string_count      = 4;
    }
    else
    {
        tusb_cfg.descriptor.device            = NULL;
        tusb_cfg.descriptor.full_speed_config = hid_configuration_descriptor_default;
        tusb_cfg.descriptor.string            = hid_string_descriptor;
        tusb_cfg.descriptor.string_count      = 4;
    }

    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));
    ESP_LOGI(TAG, "USB init done");
}

void usb_init(void)
{
    usb_init_mode(current_sim_mode);
}

bool usb_is_connected(void)
{
    return tud_mounted();
}