#include "rc_crsf.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "CRSF_ENGINE";
#define CRSF_DEFAULT_BAUD_RATE             400000U
#define CRSF_DEFAULT_TASK_PRIORITY         9
#define CRSF_DEFAULT_TASK_CORE             1
#define CRSF_MANUAL_PING_MIN_INTERVAL_MS   250U
#define CRSF_SYNC_BYTE                    0xC8
#define CRSF_ADDRESS_TX_MODULE            0xEE  
#define CRSF_ADDRESS_RADIO                0xEA  
#define CRSF_FRAMETYPE_RC_CHANNELS        0x16  
#define CRSF_FRAMETYPE_LINK_STAT          0x14
#define CRSF_FRAMETYPE_DEVICE_PING        0x28  
#define CRSF_FRAMETYPE_DEVICE_INFO        0x29  
#define CRSF_FRAMETYPE_PARAMETER_SETTINGS_ENTRY 0x2B  
#define CRSF_FRAMETYPE_PARAMETER_READ     0x2C  
#define CRSF_FRAMETYPE_PARAMETER_WRITE    0x2D  
#define CRSF_PARAM_TYPE_SELECT            0x09
#define CRSF_PARAM_TYPE_STRING            0x0A
#define CRSF_PARAM_TYPE_FOLDER            0x0B
#define CRSF_PARAM_TYPE_INFO              0x0C
#define CRSF_PARAM_TYPE_COMMAND           0x0D
#define CRSF_PARAM_TYPE_OUT_OF_RANGE      0x7F
#define CRSF_CRC_POLY 0xD5

static crsf_config_t s_cfg;
static crsf_state_t s_state; 
static volatile uint8_t s_target_addr = CRSF_ADDRESS_TX_MODULE; 
static uint16_t s_channels_11bit[16]; 
static uint8_t s_current_req_id = 1; 
static volatile uint8_t s_current_req_chunk = 0;
static volatile bool s_waiting_param_resp = false;
static size_t s_current_param_data_len = 0;
static uint32_t s_last_link_time_ms = 0;
static uint32_t s_last_manual_ping_ms = 0;
static volatile bool s_force_ping_requested = false;
static volatile bool s_force_menu_reload_requested = false;
static char s_last_device_name[64] = {0};

static uint8_t crsf_crc8(const uint8_t *data, size_t len);

static void crsf_send_ping_frame(uint8_t dest_addr) {
    uint8_t frame[6];
    frame[0] = dest_addr;
    frame[1] = 4;
    frame[2] = CRSF_FRAMETYPE_DEVICE_PING;
    frame[3] = CRSF_ADDRESS_TX_MODULE;
    frame[4] = CRSF_ADDRESS_RADIO;
    frame[5] = crsf_crc8(&frame[2], 3);
    uart_write_bytes(s_cfg.uart_port, frame, sizeof(frame));
}

static uint8_t crsf_crc8(const uint8_t *data, size_t len) {
    uint8_t crc = 0;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ CRSF_CRC_POLY) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

crsf_state_t* crsf_get_state(void) { return &s_state; }

void crsf_set_channel(uint8_t channel_idx, uint16_t value_us) {
    if (channel_idx >= 16) return;
    s_channels_11bit[channel_idx] = 172 + ((value_us - 1000) * (1811 - 172)) / 1000;
}

void crsf_send_device_ping(void) {
    if (s_state.is_ready) {
        return;
    }
    uint32_t now_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
    if (now_ms - s_last_manual_ping_ms < CRSF_MANUAL_PING_MIN_INTERVAL_MS) {
        return;
    }
    s_last_manual_ping_ms = now_ms;
    s_force_ping_requested = true;
}

void crsf_request_menu_reload(void) {
    uint32_t now_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
    s_force_menu_reload_requested = true;

    if (now_ms - s_last_manual_ping_ms >= CRSF_MANUAL_PING_MIN_INTERVAL_MS) {
        s_last_manual_ping_ms = now_ms;
        s_force_ping_requested = true;
    }
}

static void reset_param_request_state(void) {
    s_current_req_chunk = 0;
    s_waiting_param_resp = false;
    s_current_param_data_len = 0;
}

static uint8_t count_loaded_params(void) {
    uint8_t count = 0;
    for (int i = 0; i < CRSF_MAX_MENU_ITEMS; ++i) {
        if (s_state.menu[i].is_valid) {
            count++;
        }
    }
    return count;
}

static size_t copy_null_terminated_field(const uint8_t *src, size_t src_len, char *dst, size_t dst_size) {
    if (!src || !dst || dst_size == 0) return 0;

    const uint8_t *nul = memchr(src, '\0', src_len);
    if (!nul) return 0;

    size_t field_len = (size_t)(nul - src);
    size_t copy_len = field_len;
    if (copy_len >= dst_size) copy_len = dst_size - 1;

    memcpy(dst, src, copy_len);
    dst[copy_len] = '\0';
    return field_len + 1;
}

static void reset_menu_item(crsf_menu_item_t *item, uint8_t param_id, uint8_t parent_id, uint8_t type) {
    memset(item->name, 0, sizeof(item->name));
    memset(item->options, 0, sizeof(item->options));
    memset(item->_raw_data, 0, sizeof(item->_raw_data));
    item->id = param_id;
    item->parent_id = parent_id;
    item->type = type;
    item->value = 0;
    item->is_valid = false;
}

static bool decode_menu_item(crsf_menu_item_t *item, size_t raw_len) {
    if (!item || raw_len == 0) return false;
    if (item->type == CRSF_PARAM_TYPE_OUT_OF_RANGE) return false;

    memset(item->name, 0, sizeof(item->name));
    memset(item->options, 0, sizeof(item->options));
    item->value = 0;

    size_t offset = copy_null_terminated_field(item->_raw_data, raw_len, item->name, sizeof(item->name));
    if (offset == 0 || offset > raw_len) return false;

    size_t remaining = raw_len - offset;
    const uint8_t *value_ptr = item->_raw_data + offset;

    switch (item->type) {
        case CRSF_PARAM_TYPE_SELECT: {
            size_t opt_len = copy_null_terminated_field(value_ptr, remaining, item->options, sizeof(item->options));
            if (opt_len == 0 || opt_len + 4 > remaining) return false;
            item->value = value_ptr[opt_len];
            return true;
        }
        case CRSF_PARAM_TYPE_STRING:
        case CRSF_PARAM_TYPE_INFO:
            if (remaining == 0) return true;
            return copy_null_terminated_field(value_ptr, remaining, item->options, sizeof(item->options)) > 0;
        case CRSF_PARAM_TYPE_COMMAND:
            if (remaining < 2) return false;
            item->value = value_ptr[0];
            if (remaining == 2) return true;
            return copy_null_terminated_field(value_ptr + 2, remaining - 2, item->options, sizeof(item->options)) > 0;
        case CRSF_PARAM_TYPE_FOLDER:
        default:
            return true;
    }
}

static void internal_req_param(uint8_t param_index, uint8_t chunk_index) {
    uint8_t frame[8];
    frame[0] = s_target_addr; frame[1] = 6; frame[2] = CRSF_FRAMETYPE_PARAMETER_READ; 
    frame[3] = s_target_addr; frame[4] = CRSF_ADDRESS_RADIO;
    frame[5] = param_index;   frame[6] = chunk_index; 
    frame[7] = crsf_crc8(&frame[2], 5); 
    uart_write_bytes(s_cfg.uart_port, frame, sizeof(frame));
}

void crsf_write_menu_value(uint8_t param_id, uint8_t new_value) {
    uint8_t frame[8];
    frame[0] = s_target_addr; frame[1] = 6; frame[2] = CRSF_FRAMETYPE_PARAMETER_WRITE;
    frame[3] = s_target_addr; frame[4] = CRSF_ADDRESS_RADIO;
    frame[5] = param_id;      frame[6] = new_value;
    frame[7] = crsf_crc8(&frame[2], 5);
    uart_write_bytes(s_cfg.uart_port, frame, sizeof(frame));
}

static void crsf_tx_task(void *arg) {
    int req_timer = 0, retry_count = 0;
    uint8_t frame[26];
    while (1) {
        uint32_t now_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
        if (s_state.is_linked && (now_ms - s_last_link_time_ms > 4000)) s_state.is_linked = false;
        
        if (!s_state.is_ready) {
            crsf_send_ping_frame(CRSF_ADDRESS_TX_MODULE);
            vTaskDelay(pdMS_TO_TICKS(1000));
        } else {
            if (s_force_ping_requested) {
                crsf_send_ping_frame(s_target_addr);
                s_force_ping_requested = false;
            }
            // 通道发送
            frame[0] = s_target_addr; frame[1] = 24; frame[2] = CRSF_FRAMETYPE_RC_CHANNELS; 
            uint32_t bit_buffer = 0; uint8_t bits_in_buffer = 0; size_t out_index = 3;
            for (int i = 0; i < 16; ++i) {
                bit_buffer |= (s_channels_11bit[i] & 0x07FF) << bits_in_buffer; 
                bits_in_buffer += 11;
                while (bits_in_buffer >= 8) {
                    frame[out_index++] = (uint8_t)(bit_buffer & 0xFF);
                    bit_buffer >>= 8; bits_in_buffer -= 8;
                }
            }
            frame[25] = crsf_crc8(&frame[2], 23); 
            uart_write_bytes(s_cfg.uart_port, frame, 26);

            // 真正的阻塞加载逻辑
            if (s_state.total_params > 0 && s_current_req_id <= s_state.total_params) {
                crsf_menu_item_t *m = &s_state.menu[s_current_req_id - 1];
                if (!m->is_valid) {
                    if (!s_waiting_param_resp) retry_count = 0;
                    if (++req_timer >= 25) { // 500ms 重试一次
                        internal_req_param(s_current_req_id, s_current_req_chunk);
                        s_waiting_param_resp = true;
                        req_timer = 0;
                        if (++retry_count > 10) {
                            ESP_LOGW(TAG, "参数 %u chunk %u 超时，跳过", s_current_req_id, s_current_req_chunk);
                            reset_param_request_state();
                            s_current_req_id++;
                            retry_count = 0;
                        }
                    }
                } else {
                    s_current_req_id++;
                    req_timer = 0;
                    retry_count = 0;
                    reset_param_request_state();
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static void crsf_rx_task(void *arg) {
    uint8_t rx_buf[128], frame[256];
    int rx_state = 0; uint8_t frame_len = 0, frame_idx = 0;
    while (1) {
        int len = uart_read_bytes(s_cfg.uart_port, rx_buf, sizeof(rx_buf), pdMS_TO_TICKS(10));
        for (int i = 0; i < len; i++) {
            uint8_t b = rx_buf[i];
            switch (rx_state) {
                case 0:
                    if (b == CRSF_SYNC_BYTE || b == CRSF_ADDRESS_RADIO || b == CRSF_ADDRESS_TX_MODULE) {
                        frame[0] = b;
                        rx_state = 1;
                    }
                    break;
                case 1: frame_len = b; if (frame_len < 2 || frame_len > 120) rx_state = 0; 
                        else { frame[1] = b; frame_idx = 2; rx_state = 2; } break;
                case 2: frame[frame_idx++] = b;
                    if (frame_idx == frame_len + 2) { 
                        if (crsf_crc8(&frame[2], frame_len - 1) == frame[frame_len + 1]) {
                            uint8_t type = frame[2]; uint8_t *payload = &frame[3];
                            if (type == CRSF_FRAMETYPE_LINK_STAT) {
                                s_last_link_time_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
                                s_state.rssi = payload[0]; s_state.lq = payload[2]; s_state.snr = (int8_t)payload[3];
                                s_state.is_linked = true;
                            } else if (type == CRSF_FRAMETYPE_DEVICE_INFO) {
                                char device_name[64] = {0};
                                uint8_t previous_target_addr = s_target_addr;
                                s_target_addr = payload[1];
                                int offset = 2;
                                while (offset < frame_len - 2 && payload[offset] != 0) offset++; 
                                size_t name_len = (size_t)(offset - 2);
                                if (name_len >= sizeof(device_name)) name_len = sizeof(device_name) - 1;
                                if (name_len > 0) {
                                    memcpy(device_name, &payload[2], name_len);
                                    device_name[name_len] = '\0';
                                } else {
                                    strncpy(device_name, "unknown", sizeof(device_name) - 1);
                                }
                                offset++;
                                if (offset + 12 < frame_len - 2) {
                                    uint8_t reported_params = payload[offset + 12];
                                    uint8_t clamped_params = reported_params;
                                    bool param_count_clamped = false;
                                    if (clamped_params > CRSF_MAX_MENU_ITEMS) {
                                        clamped_params = CRSF_MAX_MENU_ITEMS;
                                        param_count_clamped = true;
                                    }

                                    bool same_device =
                                        s_state.is_ready &&
                                        !s_force_menu_reload_requested &&
                                        (previous_target_addr == s_target_addr) &&
                                        (s_state.total_params == clamped_params) &&
                                        (strncmp(s_last_device_name, device_name, sizeof(s_last_device_name)) == 0);

                                    if (!same_device) {
                                        if (param_count_clamped) {
                                            ESP_LOGW(TAG, "设备上报 %u 个参数，已裁剪为本地上限 %u", reported_params, CRSF_MAX_MENU_ITEMS);
                                        }
                                        strncpy(s_state.device_name, device_name, sizeof(s_state.device_name) - 1);
                                        s_state.device_name[sizeof(s_state.device_name) - 1] = '\0';
                                        s_state.total_params = clamped_params;
                                        s_state.loaded_params = 0;
                                        s_current_req_id = 1;
                                        reset_param_request_state();
                                        memset(s_state.menu, 0, sizeof(s_state.menu));
                                        s_state.is_ready = true;
                                        strncpy(s_last_device_name, device_name, sizeof(s_last_device_name) - 1);
                                        s_last_device_name[sizeof(s_last_device_name) - 1] = '\0';
                                        ESP_LOGI(TAG, "已识别高频头: %s | 参数数:%u | 目标地址:0x%02X",
                                                 device_name, s_state.total_params, s_target_addr);
                                        if (s_cfg.on_device_info_cb) {
                                            s_cfg.on_device_info_cb(device_name);
                                        }
                                        s_force_menu_reload_requested = false;
                                    } else if (s_state.device_name[0] == '\0') {
                                        strncpy(s_state.device_name, device_name, sizeof(s_state.device_name) - 1);
                                        s_state.device_name[sizeof(s_state.device_name) - 1] = '\0';
                                        s_force_menu_reload_requested = false;
                                    }
                                }
                            } else if (type == CRSF_FRAMETYPE_PARAMETER_SETTINGS_ENTRY) {
                                uint8_t p_id = payload[2];
                                uint8_t p_chunks_remaining = payload[3];
                                if (p_id > 0 && p_id <= CRSF_MAX_MENU_ITEMS && p_id == s_current_req_id && s_waiting_param_resp) {
                                    crsf_menu_item_t *m = &s_state.menu[p_id - 1];
                                    const uint8_t *chunk_ptr = NULL;
                                    size_t chunk_len = 0;

                                    if (s_current_req_chunk == 0) {
                                        if (frame_len < 8) {
                                            s_waiting_param_resp = false;
                                            break;
                                        }
                                        uint8_t p_parent = payload[4];
                                        uint8_t p_type = payload[5] & 0x7F;
                                        reset_menu_item(m, p_id, p_parent, p_type);
                                        chunk_ptr = &payload[6];
                                        chunk_len = (size_t)(frame_len - 8);
                                    } else {
                                        if (frame_len < 6) {
                                            s_waiting_param_resp = false;
                                            break;
                                        }
                                        chunk_ptr = &payload[4];
                                        chunk_len = (size_t)(frame_len - 6);
                                    }

                                    if (m->id == p_id) {
                                        if (s_current_param_data_len + chunk_len <= sizeof(m->_raw_data)) {
                                            memcpy(m->_raw_data + s_current_param_data_len, chunk_ptr, chunk_len);
                                            s_current_param_data_len += chunk_len;
                                            s_waiting_param_resp = false;

                                            if (p_chunks_remaining > 0) {
                                                s_current_req_chunk++;
                                            } else {
                                                m->is_valid = decode_menu_item(m, s_current_param_data_len);
                                                if (!m->is_valid) {
                                                    ESP_LOGW(TAG, "参数 %u 解码失败", p_id);
                                                }
                                                s_state.loaded_params = count_loaded_params();
                                                reset_param_request_state();
                                            }
                                        } else {
                                            ESP_LOGW(TAG, "参数 %u 数据过长，丢弃", p_id);
                                            reset_menu_item(m, p_id, 0, CRSF_PARAM_TYPE_OUT_OF_RANGE);
                                            reset_param_request_state();
                                        }
                                    }
                                }
                            }
                        }
                        rx_state = 0;
                    }
                    break;
            }
        }
        vTaskDelay(1);
    }
}

void crsf_init(const crsf_config_t *config) {
    s_cfg = *config;
    if (s_cfg.baud_rate == 0) s_cfg.baud_rate = CRSF_DEFAULT_BAUD_RATE;
    if (s_cfg.task_priority <= 0) s_cfg.task_priority = CRSF_DEFAULT_TASK_PRIORITY;
    if (s_cfg.task_core_id < 0) s_cfg.task_core_id = CRSF_DEFAULT_TASK_CORE;
    memset(&s_state, 0, sizeof(crsf_state_t));
    memset(s_last_device_name, 0, sizeof(s_last_device_name));
    for (int i = 0; i < 16; ++i) {
        s_channels_11bit[i] = 992;
    }
    const uart_config_t u_cfg = { .baud_rate = s_cfg.baud_rate, .data_bits = UART_DATA_8_BITS, .parity = UART_PARITY_DISABLE, .stop_bits = UART_STOP_BITS_1, .flow_ctrl = UART_HW_FLOWCTRL_DISABLE, .source_clk = UART_SCLK_DEFAULT };
    uart_driver_install(s_cfg.uart_port, 1024, 1024, 0, NULL, 0);
    uart_param_config(s_cfg.uart_port, &u_cfg);
    uart_set_pin(s_cfg.uart_port, s_cfg.tx_pin, s_cfg.rx_pin, -1, -1);
    ESP_LOGI(TAG, "CRSF init: uart=%d tx=%d rx=%d baud=%lu prio=%d core=%d",
             (int)s_cfg.uart_port, s_cfg.tx_pin, s_cfg.rx_pin,
             (unsigned long)s_cfg.baud_rate, s_cfg.task_priority, s_cfg.task_core_id);
    xTaskCreatePinnedToCore(crsf_rx_task, "crsf_rx", 4096, NULL, s_cfg.task_priority + 1, NULL, s_cfg.task_core_id);
    xTaskCreatePinnedToCore(crsf_tx_task, "crsf_tx", 4096, NULL, s_cfg.task_priority, NULL, s_cfg.task_core_id);
}
