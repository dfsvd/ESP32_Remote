#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "rc_read.h"
#include "driver/gpio.h"
#include "driver/uart.h"

// CRSF 使用独立 UART 外设发送到外置高频头。
#define CRSF_UART_PORT       UART_NUM_1
#define CRSF_UART_BAUD_RATE  420000

// 传输模式选择：
// - CRSF_LINK_MODE_UART_FULL_DUPLEX: 传统双线 UART，TX/RX 分开接线。
// - CRSF_LINK_MODE_UART_HALF_DUPLEX: 单线半双工，TX/RX 复用同一根 CRSF 信号线。
typedef enum {
    CRSF_LINK_MODE_UART_FULL_DUPLEX = 0,
    CRSF_LINK_MODE_UART_HALF_DUPLEX = 1,
} crsf_link_mode_t;

// 默认保持当前双线模式，避免影响现有硬件接法。
#define CRSF_LINK_MODE      CRSF_LINK_MODE_UART_FULL_DUPLEX

// 双线模式下使用的独立 TX / RX 引脚。
#define CRSF_UART_TX_PIN     GPIO_NUM_21
#define CRSF_UART_RX_PIN     GPIO_NUM_20

// 单线模式下使用的共享 CRSF 信号引脚。
// 切到 CRSF_LINK_MODE_UART_HALF_DUPLEX 时，把高频头的 CRSF 信号线接到这个 GPIO。
#define CRSF_UART_HALF_DUPLEX_PIN GPIO_NUM_21

#define CRSF_UART_RTS_PIN    UART_PIN_NO_CHANGE
#define CRSF_UART_CTS_PIN    UART_PIN_NO_CHANGE

#define CRSF_TASK_STACK_SIZE 4096
#define CRSF_TASK_PRIORITY   5
#define CRSF_TASK_PERIOD_MS  4
#define CRSF_TASK_CORE_ID    tskNO_AFFINITY

typedef enum {
    CRSF_POWER_10MW = 0,
    CRSF_POWER_25MW,
    CRSF_POWER_100MW,
    CRSF_POWER_250MW,
    CRSF_POWER_500MW,
    CRSF_POWER_1000MW,
    CRSF_POWER_COUNT
} crsf_power_t;

// 启动 CRSF 发送任务。通道数据复用现有 joy 结构体。
void crsf_init(fpv_joystick_report_t *joy);

// 设置/获取当前功率档位，功率值会通过现有网页和 NVS 流程持久化。
void crsf_set_power(uint8_t power_index);
uint8_t crsf_get_power(void);

// 请求发送一次 Bind 指令。Bind 是动作，不是持久状态。
void crsf_request_bind(void);
bool crsf_consume_bind_request(void);

#ifdef __cplusplus
}
#endif
