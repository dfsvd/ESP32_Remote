#pragma once
#include "driver/uart.h"
#include <stdbool.h>
#include <stdint.h>

#define CRSF_MAX_MENU_ITEMS 30

// 菜单项结构体
typedef struct {
    uint8_t id;
    uint8_t parent_id;
    uint8_t type;
    char name[64];
    char options[256]; // SELECT 选项，INFO/STRING/COMMAND
                       // 等文本型参数也复用此缓冲
    uint8_t value;
    bool is_valid;          // 标记是否已完整加载
    uint8_t _raw_data[384]; // 🔥 扩容：从 192 增加到 384
} crsf_menu_item_t;

// 回传传感器数据结构体
typedef struct {
    struct {
        uint16_t voltage;       // 电池电压 V*10 (如 66 = 6.6V)
        uint16_t current;       // 电流 A*10 (如 3 = 0.3A)
        uint32_t capacity;      // 消耗容量 mAh
        uint8_t remaining;      // 剩余百分比 %
    } battery;
    struct {
        int32_t latitude;       // 纬度 度*1e7 (大端)
        int32_t longitude;      // 经度 度*1e7 (大端)
        uint16_t altitude;      // 海拔 m + 1000m
        uint16_t speed;         // 地速 km/h / 10
        uint16_t heading;       // 航向 度 / 100
        uint8_t sats;           // 锁定卫星数
    } gps;
    struct {
        int16_t pitch;          // 俯仰 度*10
        int16_t roll;           // 横滚 度*10
        int16_t yaw;            // 航向 度*10
    } attitude;
    struct {
        int16_t altitude;       // 气压计海拔 cm
        int16_t vSpeed;         // 垂直速度 cm/s
    } vario;
    uint8_t flight_mode[16];    // 飞行模式字符串
    uint32_t last_update_ms;    // 最近一次回传更新时间戳
} crsf_telemetry_t;

// 全局状态机
typedef struct {
    bool is_ready;
    bool is_linked;
    uint8_t rssi;
    uint8_t lq;
    int8_t snr;
    char device_name[64];
    uint8_t total_params;
    uint8_t loaded_params;
    crsf_menu_item_t menu[CRSF_MAX_MENU_ITEMS];
    crsf_telemetry_t telemetry;
    bool trigger_bind;
} crsf_state_t;

typedef struct {
    uart_port_t uart_port;
    uint32_t baud_rate;
    int tx_pin;
    int rx_pin;
    bool half_duplex;
    bool invert_signal;
    int task_priority;
    int task_core_id;
    void (*on_device_info_cb)(const char *name);
} crsf_config_t;

void crsf_init(const crsf_config_t *config);
crsf_state_t *crsf_get_state(void);
void crsf_set_channel(uint8_t channel_idx, uint16_t value_us);
void crsf_write_menu_value(uint8_t param_id, uint8_t new_value);
void crsf_send_device_ping(void);
void crsf_request_menu_reload(void);
void crsf_set_link_mode(bool half_duplex);
bool crsf_is_half_duplex(void);

/**
 * @brief 发送 MSP 数据到飞控（封装为 CRSF 0x7A 帧）
 * @param data 原始 MSP 字节流
 * @param len  数据长度 (最大 115 字节)
 */
void crsf_send_msp(const uint8_t *data, size_t len);

/**
 * @brief 读取飞控返回的 MSP 响应
 * @param buf     接收缓冲
 * @param max_len 缓冲大小
 * @return 实际读取的字节数，0 = 无数据
 */
int crsf_read_msp(uint8_t *buf, size_t max_len);

/**
 * @brief 检查是否有 MSP 响应待读取
 */
bool crsf_msp_available(void);

/**
 * @brief 重置 MSP 接收缓冲（如切换模式时）
 */
void crsf_reset_msp(void);
