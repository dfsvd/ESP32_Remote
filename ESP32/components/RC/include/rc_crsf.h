#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "driver/uart.h"

#define CRSF_MAX_MENU_ITEMS 30

// 菜单项结构体
typedef struct {
    uint8_t id;
    uint8_t parent_id;
    uint8_t type;
    char name[64];
    char options[256];   // SELECT 选项，INFO/STRING/COMMAND 等文本型参数也复用此缓冲
    uint8_t value;
    bool is_valid;       // 标记是否已完整加载
    uint8_t _raw_data[384]; // 🔥 扩容：从 192 增加到 384
} crsf_menu_item_t;

// 全局状态机
typedef struct {
    bool is_ready;
    bool is_linked;
    uint8_t rssi;
    uint8_t lq;
    int8_t  snr;
    char device_name[64];
    uint8_t total_params;
    uint8_t loaded_params;
    crsf_menu_item_t menu[CRSF_MAX_MENU_ITEMS];
    bool trigger_bind;
} crsf_state_t;

typedef struct {
    uart_port_t uart_port;
    uint32_t baud_rate;
    int tx_pin;
    int rx_pin;
    int task_priority;
    int task_core_id;
    void (*on_device_info_cb)(const char *name);
} crsf_config_t;

void crsf_init(const crsf_config_t *config);
crsf_state_t* crsf_get_state(void);
void crsf_set_channel(uint8_t channel_idx, uint16_t value_us);
void crsf_write_menu_value(uint8_t param_id, uint8_t new_value);
void crsf_send_device_ping(void);
void crsf_request_menu_reload(void);
