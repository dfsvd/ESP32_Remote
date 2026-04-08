#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "esp_log.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rc_crsf.h"

#define CRSF_ADDRESS_RADIO_TRANSMITTER 0xEE
#define CRSF_ADDRESS_CRSF_RECEIVER     0xEC
#define CRSF_FRAME_TYPE_RC_CHANNELS_PACKED 0x16
#define CRSF_FRAME_TYPE_COMMAND        0x32
#define CRSF_PAYLOAD_SIZE_RC_CHANNELS  22
#define CRSF_CRC_POLY                  0xD5
#define CRSF_FRAME_SIZE                (2 + 1 + CRSF_PAYLOAD_SIZE_RC_CHANNELS + 1)
#define CRSF_CHANNEL_MIN_US            1000U
#define CRSF_CHANNEL_MAX_US            2000U
#define CRSF_CHANNEL_MIN_VALUE         172U
#define CRSF_CHANNEL_MAX_VALUE         1811U
#define CRSF_CHANNEL_COUNT             16U
#define CRSF_COMMAND_SUBCMD_GENERAL    0x10
#define CRSF_COMMAND_PARAM_BIND        0x01
#define CRSF_COMMAND_PARAM_POWER       0x02
#define CRSF_COMMAND_FRAME_SIZE        7U

static const char *TAG = "CRSF";
static fpv_joystick_report_t *s_joy;
static volatile uint8_t s_power_index = CRSF_POWER_25MW;
static volatile bool s_bind_requested = false;

// CRSF 使用 CRC8 校验，当前多种 frame 都复用同一套计算逻辑。
static uint8_t crsf_crc8_update(uint8_t crc, uint8_t data)
{
    crc ^= data;
    for (int bit = 0; bit < 8; ++bit) {
        crc = (crc & 0x80U) ? (uint8_t)((crc << 1) ^ CRSF_CRC_POLY) : (uint8_t)(crc << 1);
    }
    return crc;
}

static uint8_t crsf_crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0;
    for (size_t i = 0; i < len; ++i) {
        crc = crsf_crc8_update(crc, data[i]);
    }
    return crc;
}

// 把当前工程内部统一使用的 1000~2000us 通道值映射到 CRSF 的 11bit 取值范围。
static uint16_t crsf_map_channel(uint16_t value_us)
{
    if (value_us <= CRSF_CHANNEL_MIN_US) {
        return CRSF_CHANNEL_MIN_VALUE;
    }
    if (value_us >= CRSF_CHANNEL_MAX_US) {
        return CRSF_CHANNEL_MAX_VALUE;
    }

    return (uint16_t)(CRSF_CHANNEL_MIN_VALUE +
        ((uint32_t)(value_us - CRSF_CHANNEL_MIN_US) * (CRSF_CHANNEL_MAX_VALUE - CRSF_CHANNEL_MIN_VALUE) + 500U) /
        (CRSF_CHANNEL_MAX_US - CRSF_CHANNEL_MIN_US));
}

// 从共享 joy 结构体中取出 16 个逻辑通道，并按 CRSF 通道顺序排好。
static void crsf_load_channels(const fpv_joystick_report_t *joy, uint16_t channels[CRSF_CHANNEL_COUNT])
{
    channels[0] = crsf_map_channel(joy->roll);
    channels[1] = crsf_map_channel(joy->pitch);
    channels[2] = crsf_map_channel(joy->throttle);
    channels[3] = crsf_map_channel(joy->yaw);
    channels[4] = crsf_map_channel(joy->sw1);
    channels[5] = crsf_map_channel(joy->sw2);
    channels[6] = crsf_map_channel(joy->sw3);
    channels[7] = crsf_map_channel(joy->sw4);
    channels[8] = crsf_map_channel(joy->sw5);
    channels[9] = crsf_map_channel(joy->sw6);
    channels[10] = crsf_map_channel(joy->sw7);
    channels[11] = crsf_map_channel(joy->sw8);
    channels[12] = crsf_map_channel(1500);
    channels[13] = crsf_map_channel(1500);
    channels[14] = crsf_map_channel(1500);
    channels[15] = crsf_map_channel(1500);
}

// CRSF RC frame 的 payload 需要把 16 个 11bit 通道连续打包成 22 字节。
static void crsf_pack_channels(const fpv_joystick_report_t *joy, uint8_t payload[CRSF_PAYLOAD_SIZE_RC_CHANNELS])
{
    uint16_t channels[CRSF_CHANNEL_COUNT];
    uint32_t bit_buffer = 0;
    uint8_t bits_in_buffer = 0;
    size_t out_index = 0;

    memset(payload, 0, CRSF_PAYLOAD_SIZE_RC_CHANNELS);
    crsf_load_channels(joy, channels);

    for (size_t i = 0; i < CRSF_CHANNEL_COUNT; ++i) {
        bit_buffer |= ((uint32_t)(channels[i] & 0x07FFU)) << bits_in_buffer;
        bits_in_buffer += 11;

        while (bits_in_buffer >= 8U && out_index < CRSF_PAYLOAD_SIZE_RC_CHANNELS) {
            payload[out_index++] = (uint8_t)(bit_buffer & 0xFFU);
            bit_buffer >>= 8;
            bits_in_buffer -= 8;
        }
    }

    if (out_index < CRSF_PAYLOAD_SIZE_RC_CHANNELS) {
        payload[out_index] = (uint8_t)(bit_buffer & 0xFFU);
    }
}

// 构造常规 RC Channels Packed frame。无论双线还是单线模式，这部分协议打包逻辑完全相同。
static size_t crsf_build_rc_frame(const fpv_joystick_report_t *joy, uint8_t frame[CRSF_FRAME_SIZE])
{
    frame[0] = CRSF_ADDRESS_RADIO_TRANSMITTER;
    frame[1] = CRSF_PAYLOAD_SIZE_RC_CHANNELS + 2;
    frame[2] = CRSF_FRAME_TYPE_RC_CHANNELS_PACKED;
    crsf_pack_channels(joy, &frame[3]);
    frame[CRSF_FRAME_SIZE - 1] = crsf_crc8(&frame[2], CRSF_PAYLOAD_SIZE_RC_CHANNELS + 1);
    return CRSF_FRAME_SIZE;
}

// 构造简单命令帧，用于当前网页触发的 Bind / Power 控制。
static size_t crsf_build_command_frame(uint8_t command, uint8_t value, uint8_t frame[CRSF_COMMAND_FRAME_SIZE])
{
    frame[0] = CRSF_ADDRESS_CRSF_RECEIVER;
    frame[1] = 4;
    frame[2] = CRSF_FRAME_TYPE_COMMAND;
    frame[3] = CRSF_COMMAND_SUBCMD_GENERAL;
    frame[4] = command;
    frame[5] = value;
    frame[6] = crsf_crc8(&frame[2], 4);
    return CRSF_COMMAND_FRAME_SIZE;
}

static void crsf_uart_apply_common_config(void)
{
    const uart_config_t uart_config = {
        .baud_rate = CRSF_UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_driver_install(CRSF_UART_PORT, 256, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(CRSF_UART_PORT, &uart_config));
}

// 双线模式：使用独立 TX / RX，引脚接法与普通 UART 一样。
static void crsf_uart_init_full_duplex(void)
{
    crsf_uart_apply_common_config();
    ESP_ERROR_CHECK(uart_set_pin(CRSF_UART_PORT, CRSF_UART_TX_PIN, CRSF_UART_RX_PIN, CRSF_UART_RTS_PIN, CRSF_UART_CTS_PIN));
}

// 单线模式：TX / RX 共用同一个 GPIO，并切到 ESP-IDF 的半双工 UART 模式。
static void crsf_uart_init_half_duplex(void)
{
    crsf_uart_apply_common_config();
    ESP_ERROR_CHECK(uart_set_pin(CRSF_UART_PORT, CRSF_UART_HALF_DUPLEX_PIN, CRSF_UART_HALF_DUPLEX_PIN, CRSF_UART_RTS_PIN, CRSF_UART_CTS_PIN));
    ESP_ERROR_CHECK(uart_set_mode(CRSF_UART_PORT, UART_MODE_RS485_HALF_DUPLEX));
}

// 物理层初始化只在这里分流，后面的帧构造和发送任务不关心单双线差异。
static void crsf_uart_init(void)
{
#if CRSF_LINK_MODE == CRSF_LINK_MODE_UART_HALF_DUPLEX
    crsf_uart_init_half_duplex();
#else
    crsf_uart_init_full_duplex();
#endif
}

// 当前只需要处理一次性命令，例如 Bind。发送后立即清掉请求标记。
static void crsf_send_pending_commands(void)
{
    uint8_t frame[CRSF_COMMAND_FRAME_SIZE];

    if (s_bind_requested) {
        crsf_build_command_frame(CRSF_COMMAND_PARAM_BIND, 1, frame);
        uart_write_bytes(CRSF_UART_PORT, frame, sizeof(frame));
        s_bind_requested = false;
    }
}

// 发送任务持续复用同一套逻辑：先处理待发命令，再周期性发送 RC 通道帧。
static void crsf_tx_task(void *arg)
{
    uint8_t frame[CRSF_FRAME_SIZE];
    fpv_joystick_report_t *joy = (fpv_joystick_report_t *)arg;
    uint8_t last_power = 0xFF;

    while (1) {
        const uint8_t power = s_power_index;
        if (power != last_power) {
            uint8_t cmd[CRSF_COMMAND_FRAME_SIZE];
            crsf_build_command_frame(CRSF_COMMAND_PARAM_POWER, power, cmd);
            uart_write_bytes(CRSF_UART_PORT, cmd, sizeof(cmd));
            last_power = power;
        }

        crsf_send_pending_commands();

        const size_t frame_len = crsf_build_rc_frame(joy, frame);
        uart_write_bytes(CRSF_UART_PORT, frame, frame_len);
        vTaskDelay(pdMS_TO_TICKS(CRSF_TASK_PERIOD_MS));
    }
}

void crsf_init(fpv_joystick_report_t *joy)
{
    s_joy = joy;
    crsf_uart_init();
    xTaskCreatePinnedToCore(crsf_tx_task, "crsf_tx", CRSF_TASK_STACK_SIZE, s_joy, CRSF_TASK_PRIORITY, NULL, CRSF_TASK_CORE_ID);

#if CRSF_LINK_MODE == CRSF_LINK_MODE_UART_HALF_DUPLEX
    ESP_LOGI(TAG, "CRSF TX started in single-wire half-duplex mode on uart=%d pin=%d baud=%d", CRSF_UART_PORT, CRSF_UART_HALF_DUPLEX_PIN, CRSF_UART_BAUD_RATE);
#else
    ESP_LOGI(TAG, "CRSF TX started in dual-wire mode on uart=%d tx=%d rx=%d baud=%d", CRSF_UART_PORT, CRSF_UART_TX_PIN, CRSF_UART_RX_PIN, CRSF_UART_BAUD_RATE);
#endif
}

void crsf_set_power(uint8_t power_index)
{
    if (power_index < CRSF_POWER_COUNT) {
        s_power_index = power_index;
    }
}

uint8_t crsf_get_power(void)
{
    return s_power_index;
}

void crsf_request_bind(void)
{
    s_bind_requested = true;
}

bool crsf_consume_bind_request(void)
{
    const bool requested = s_bind_requested;
    s_bind_requested = false;
    return requested;
}
