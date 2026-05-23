#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <ctype.h>
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "nvs.h"              // 必须引入 NVS 库
#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rc_wf.h"
#include "rc_usb.h"
#include "rc_crsf.h"
#include "logo_icon.h"
#include "logo_horizontal.h"

static const char *TAG = "RC_WIFI";
static httpd_handle_t server = NULL;
#define CRSF_STATUS_BUF_SIZE 256
#define CRSF_MENU_BUF_SIZE   12288
static bool s_saved_crsf_half_duplex = false;
static bool s_has_saved_crsf_link_mode = false;

// =================================================================================
// 快速 DNS 响应 — 连通性检测域名→ESP32(204通过)，其他域名→NXDOMAIN(防止请求淹没)
// =================================================================================
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define DNS_PORT 53

static void dns_server_task(void *pvParameters) {
    struct sockaddr_in bind_addr = {
        .sin_addr.s_addr = htonl(INADDR_ANY),
        .sin_family = AF_INET,
        .sin_port = htons(DNS_PORT),
    };

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) { vTaskDelete(NULL); return; }
    if (bind(sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) != 0) {
        close(sock); vTaskDelete(NULL); return;
    }

    uint8_t buf[512];
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);

    while (1) {
        int len = recvfrom(sock, buf, sizeof(buf), 0,
                           (struct sockaddr *)&client_addr, &client_len);
        if (len < 12) continue;

        // 跳过问题名称 (不定长)，提取域名用于判断
        int qname_len = 0;
        while (len > 12 + qname_len && buf[12 + qname_len] != 0)
            qname_len += buf[12 + qname_len] + 1;
        qname_len++;
        if (12 + qname_len + 4 > len) continue;

        // 只响应 A 记录查询
        uint16_t qtype  = (buf[12 + qname_len] << 8) | buf[12 + qname_len + 1];
        uint16_t qclass = (buf[12 + qname_len + 2] << 8) | buf[12 + qname_len + 3];
        if (qtype != 1 || qclass != 1) continue;

        // 判断是否是连通性检测域名
        bool is_connectivity_check = false;
        int src = 12;
        char domain_buf[128];
        int di = 0;
        while (src < 12 + qname_len - 1 && di < 120) {
            uint8_t label_len = buf[src];
            if (label_len == 0) break;
            for (uint8_t j = 0; j < label_len && di < 120; j++)
                domain_buf[di++] = (char)tolower(buf[src + 1 + j]);
            domain_buf[di++] = '.';
            src += 1 + label_len;
        }
        if (di > 0) domain_buf[di - 1] = '\0';
        if (strstr(domain_buf, "connectivitycheck") ||
            strstr(domain_buf, "gstatic") ||
            strstr(domain_buf, "google") ||
            strstr(domain_buf, "miui") ||
            strstr(domain_buf, "mi.com"))
            is_connectivity_check = true;

        if (is_connectivity_check) {
            // 连通性检测 → 指向 ESP32，配合 /generate_204 返回 204
            buf[2] = 0x81; buf[3] = 0x80;
            buf[6] = 0; buf[7] = 1;
            buf[8] = 0; buf[9] = 0;
            buf[10] = 0; buf[11] = 0;

            int off = 12 + qname_len + 4;
            buf[off]     = 0xC0; buf[off + 1] = 0x0C;
            buf[off + 2] = 0x00; buf[off + 3] = 0x01;
            buf[off + 4] = 0x00; buf[off + 5] = 0x01;
            buf[off + 6] = 0x00; buf[off + 7] = 0x00;
            buf[off + 8] = 0x00; buf[off + 9] = 0x3C;
            buf[off + 10] = 0x00; buf[off + 11] = 0x04;
            buf[off + 12] = 192; buf[off + 13] = 168;
            buf[off + 14] = 4;    buf[off + 15] = 1;

            sendto(sock, buf, off + 16, 0,
                   (struct sockaddr *)&client_addr, client_len);
        } else {
            // 其他域名 → NXDOMAIN，App 不会连接
            buf[2] = 0x83; buf[3] = 0x83;
            buf[6] = 0; buf[7] = 0;
            buf[8] = 0; buf[9] = 0;
            buf[10] = 0; buf[11] = 0;

            sendto(sock, buf, len, 0,
                   (struct sockaddr *)&client_addr, client_len);
        }
    }

    close(sock);
    vTaskDelete(NULL);
}

// 1. 本地的 16 通道测试数组 (出厂默认值)
extern channel_cal_t limit[16] ;
extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[]   asm("_binary_index_html_end");

static void json_append_escaped(char *dst, size_t dst_size, size_t *offset, const char *src)
{
    if (!dst || !offset || dst_size == 0) {
        return;
    }

    if (!src) {
        src = "";
    }

    for (size_t i = 0; src[i] != '\0' && *offset + 1 < dst_size; ++i) {
        unsigned char c = (unsigned char)src[i];
        if (c == 0xC0) {
            int written = snprintf(dst + *offset, dst_size - *offset, ":Lo");
            if (written <= 0 || (size_t)written >= dst_size - *offset) break;
            *offset += (size_t)written;
            continue;
        }
        if (c == 0xC1) {
            int written = snprintf(dst + *offset, dst_size - *offset, ":Hi");
            if (written <= 0 || (size_t)written >= dst_size - *offset) break;
            *offset += (size_t)written;
            continue;
        }
        if (c == '"' || c == '\\') {
            if (*offset + 2 >= dst_size) break;
            dst[(*offset)++] = '\\';
            dst[(*offset)++] = (char)c;
            continue;
        }
        if (c == '\n') {
            if (*offset + 2 >= dst_size) break;
            dst[(*offset)++] = '\\';
            dst[(*offset)++] = 'n';
            continue;
        }
        if (c == '\r') {
            if (*offset + 2 >= dst_size) break;
            dst[(*offset)++] = '\\';
            dst[(*offset)++] = 'r';
            continue;
        }
        if (c == '\t') {
            if (*offset + 2 >= dst_size) break;
            dst[(*offset)++] = '\\';
            dst[(*offset)++] = 't';
            continue;
        }
        if (c < 32) {
            continue;
        }
        if (c > 126) {
            int written = snprintf(dst + *offset, dst_size - *offset, "\\u%04X", c);
            if (written <= 0 || (size_t)written >= dst_size - *offset) break;
            *offset += (size_t)written;
            continue;
        }
        dst[(*offset)++] = (char)c;
    }

    dst[*offset] = '\0';
}

static size_t build_crsf_status_payload(char *buf, size_t buf_size)
{
    if (!buf || buf_size == 0) {
        return 0;
    }

    crsf_state_t *state = crsf_get_state();
    int written = snprintf(
        buf,
        buf_size,
        "CRSF_STATUS:{\"is_ready\":%s,\"is_linked\":%s,\"rssi\":%u,\"lq\":%u,\"snr\":%d,"
        "\"loaded_params\":%u,\"total_params\":%u,\"wire_mode\":\"%s\",\"device_label\":\"",
        state->is_ready ? "true" : "false",
        state->is_linked ? "true" : "false",
        state->rssi,
        state->lq,
        state->snr,
        state->loaded_params,
        state->total_params,
        crsf_is_half_duplex() ? "single" : "dual"
    );

    if (written < 0 || (size_t)written >= buf_size) {
        buf[0] = '\0';
        return 0;
    }

    size_t offset = (size_t)written;
    json_append_escaped(buf, buf_size, &offset, state->device_name[0] ? state->device_name : "ESP32 bridge");

    if (offset + 4 >= buf_size) {
        buf[0] = '\0';
        return 0;
    }

    buf[offset++] = '"';
    buf[offset++] = '}';
    buf[offset++] = '\n';
    buf[offset] = '\0';
    return offset;
}

static size_t build_crsf_menu_payload(char *buf, size_t buf_size)
{
    if (!buf || buf_size == 0) {
        return 0;
    }

    crsf_state_t *state = crsf_get_state();
    size_t offset = 0;
    int written = snprintf(buf, buf_size, "CRSF_MENU:[");
    if (written < 0 || (size_t)written >= buf_size) {
        return 0;
    }
    offset = (size_t)written;

    bool first = true;
    for (int i = 0; i < CRSF_MAX_MENU_ITEMS; ++i) {
        crsf_menu_item_t *item = &state->menu[i];
        if (item->id == 0 || !item->is_valid) {
            continue;
        }

        written = snprintf(
            buf + offset,
            buf_size - offset,
            "%s{\"id\":%u,\"parent_id\":%u,\"type\":%u,\"name\":\"",
            first ? "" : ",",
            item->id,
            item->parent_id,
            item->type
        );
        if (written < 0 || (size_t)written >= buf_size - offset) {
            break;
        }
        offset += (size_t)written;
        json_append_escaped(buf, buf_size, &offset, item->name);

        written = snprintf(
            buf + offset,
            buf_size - offset,
            "\",\"value\":%u,\"options\":\"",
            item->value
        );
        if (written < 0 || (size_t)written >= buf_size - offset) {
            break;
        }
        offset += (size_t)written;
        json_append_escaped(buf, buf_size, &offset, item->options);

        if (offset + 3 >= buf_size) {
            break;
        }
        buf[offset++] = '"';
        buf[offset++] = '}';
        buf[offset] = '\0';
        first = false;
    }

    if (offset + 3 >= buf_size) {
        buf[0] = '\0';
        return 0;
    }

    buf[offset++] = ']';
    buf[offset++] = '\n';
    buf[offset] = '\0';
    return offset;
}

static bool crsf_status_needs_broadcast(
    const crsf_state_t *state,
    bool *last_ready,
    bool *last_linked,
    uint8_t *last_loaded_params,
    uint8_t *last_total_params,
    uint8_t *last_rssi,
    uint8_t *last_lq,
    int8_t *last_snr,
    char *last_device_name,
    size_t last_device_name_size)
{
    if (!state || !last_ready || !last_linked || !last_loaded_params || !last_total_params ||
        !last_rssi || !last_lq || !last_snr || !last_device_name || last_device_name_size == 0) {
        return false;
    }

    bool changed =
        *last_ready != state->is_ready ||
        *last_linked != state->is_linked ||
        *last_loaded_params != state->loaded_params ||
        *last_total_params != state->total_params ||
        *last_rssi != state->rssi ||
        *last_lq != state->lq ||
        *last_snr != state->snr ||
        strncmp(last_device_name, state->device_name, last_device_name_size) != 0;

    if (!changed) {
        return false;
    }

    *last_ready = state->is_ready;
    *last_linked = state->is_linked;
    *last_loaded_params = state->loaded_params;
    *last_total_params = state->total_params;
    *last_rssi = state->rssi;
    *last_lq = state->lq;
    *last_snr = state->snr;
    strncpy(last_device_name, state->device_name, last_device_name_size - 1);
    last_device_name[last_device_name_size - 1] = '\0';
    return true;
}

static const crsf_menu_item_t *find_crsf_menu_item_by_id(const crsf_state_t *state, uint8_t param_id)
{
    if (!state || param_id == 0) {
        return NULL;
    }

    for (int i = 0; i < CRSF_MAX_MENU_ITEMS; ++i) {
        const crsf_menu_item_t *item = &state->menu[i];
        if (item->id == param_id && item->is_valid) {
            return item;
        }
    }

    return NULL;
}

static const char *describe_crsf_option_label(
    const crsf_menu_item_t *item,
    uint8_t value,
    char *buf,
    size_t buf_size)
{
    if (!buf || buf_size == 0) {
        return "";
    }

    buf[0] = '\0';

    if (!item || item->options[0] == '\0') {
        return buf;
    }

    const char *segment_start = item->options;
    uint8_t option_index = 0;

    for (const char *cursor = item->options; ; ++cursor) {
        if (*cursor != ';' && *cursor != '\0') {
            continue;
        }

        if (option_index == value) {
            const char *trim_start = segment_start;
            const char *trim_end = cursor;

            while (trim_start < trim_end &&
                   (*trim_start == ' ' || *trim_start == '\t' || *trim_start == '\r' || *trim_start == '\n')) {
                ++trim_start;
            }

            while (trim_end > trim_start &&
                   (trim_end[-1] == ' ' || trim_end[-1] == '\t' || trim_end[-1] == '\r' || trim_end[-1] == '\n')) {
                --trim_end;
            }

            size_t copy_len = (size_t)(trim_end - trim_start);
            if (copy_len >= buf_size) {
                copy_len = buf_size - 1;
            }
            memcpy(buf, trim_start, copy_len);
            buf[copy_len] = '\0';
            return buf;
        }

        if (*cursor == '\0') {
            break;
        }

        segment_start = cursor + 1;
        ++option_index;
    }

    snprintf(buf, buf_size, "#%u", value);
    return buf;
}

static void log_crsf_web_action(const char *action, uint8_t param_id, int value, bool has_value)
{
    crsf_state_t *state = crsf_get_state();
    const crsf_menu_item_t *item = find_crsf_menu_item_by_id(state, param_id);
    const char *name = (item && item->name[0] != '\0') ? item->name : "-";

    if (!has_value) {
        ESP_LOGI(TAG, "网页命令 %s: id=%u name=%s", action, param_id, name);
        return;
    }

    char option_buf[96];
    const char *option_label = describe_crsf_option_label(item, (uint8_t)value, option_buf, sizeof(option_buf));
    if (option_label[0] != '\0') {
        ESP_LOGI(TAG, "网页命令 %s: id=%u name=%s value=%d option=%s", action, param_id, name, value, option_label);
        return;
    }

    ESP_LOGI(TAG, "网页命令 %s: id=%u name=%s value=%d", action, param_id, name, value);
}

static void ws_broadcast_text(const char *payload)
{
    if (!server || !payload || payload[0] == '\0') {
        return;
    }

    httpd_ws_frame_t ws_pkt = {
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)payload,
        .len = strlen(payload),
    };

    size_t max_clients = 8;
    int client_fds[8] = {0};
    if (httpd_get_client_list(server, &max_clients, client_fds) != ESP_OK) {
        return;
    }

    for (size_t i = 0; i < max_clients; ++i) {
        if (httpd_ws_get_fd_info(server, client_fds[i]) == HTTPD_WS_CLIENT_WEBSOCKET) {
            esp_err_t ret = httpd_ws_send_frame_async(server, client_fds[i], &ws_pkt);
            if (ret != ESP_OK) {
                ESP_LOGD(TAG, "ws_broadcast_text 发送失败 fd=%d ret=%s",
                         client_fds[i], esp_err_to_name(ret));
            }
        }
    }
}

static esp_err_t ws_send_text(httpd_req_t *req, const char *payload)
{
    if (!req || !payload || payload[0] == '\0') {
        return ESP_OK;
    }

    httpd_ws_frame_t ws_pkt = {
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)payload,
        .len = strlen(payload),
    };
    return httpd_ws_send_frame(req, &ws_pkt);
}

static esp_err_t ws_send_crsf_snapshot(httpd_req_t *req)
{
    char status_buf[CRSF_STATUS_BUF_SIZE];
    static char menu_buf[CRSF_MENU_BUF_SIZE];

    build_crsf_status_payload(status_buf, sizeof(status_buf));
    build_crsf_menu_payload(menu_buf, sizeof(menu_buf));
    ws_send_text(req, status_buf);
    return ws_send_text(req, menu_buf);
}

bool get_saved_crsf_link_mode(bool *half_duplex)
{
    if (half_duplex) {
        *half_duplex = s_saved_crsf_half_duplex;
    }
    return s_has_saved_crsf_link_mode;
}

// =================================================================================
// ⭐ NVS 存储功能 (必须写在被调用之前)
// =================================================================================
static volatile bool nvs_dirty = false;

// WebSocket handler 只标记，不阻塞
void request_nvs_save(void) {
    nvs_dirty = true;
}

// 广播任务里实际写 Flash (50ms 周期检查，不阻塞 WS 通信)
static void do_nvs_save(void) {
    if (!nvs_dirty) return;
    nvs_dirty = false;

    nvs_handle_t my_handle;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &my_handle);
    if (err == ESP_OK) {
        esp_err_t err_cal = nvs_set_blob(my_handle, "cal_data", limit, sizeof(limit));
        esp_err_t err_mode = nvs_set_u8(my_handle, "sim_mode", (uint8_t)current_sim_mode);
        esp_err_t err_crsf = nvs_set_u8(my_handle, "crsf_half", s_saved_crsf_half_duplex ? 1 : 0);
        esp_err_t err_epa_pos = nvs_set_blob(my_handle, "epa_pos", epa_pos, sizeof(epa_pos));
        esp_err_t err_epa_neg = nvs_set_blob(my_handle, "epa_neg", epa_neg, sizeof(epa_neg));
        esp_err_t err_rev = nvs_set_u16(my_handle, "rev_mask", rev_mask);
        esp_err_t err_chmap = nvs_set_blob(my_handle, "ch_map", ch_map, sizeof(ch_map));
        esp_err_t err_stick = nvs_set_u8(my_handle, "stick_mode", stick_mode);
        esp_err_t err_btn = nvs_set_blob(my_handle, "btn_cfg", btn_cfg, sizeof(btn_cfg));

        bool all_ok = (err_cal == ESP_OK && err_mode == ESP_OK && err_crsf == ESP_OK &&
                       err_epa_pos == ESP_OK && err_epa_neg == ESP_OK && err_rev == ESP_OK &&
                       err_chmap == ESP_OK && err_stick == ESP_OK && err_btn == ESP_OK);

        if (all_ok) {
            esp_err_t err_commit = nvs_commit(my_handle);
            if (err_commit == ESP_OK) {
                ESP_LOGI(TAG, "💾 设置已保存: sim=%d wire=%s rev=0x%04x",
                    (int)current_sim_mode,
                    s_saved_crsf_half_duplex ? "single" : "dual",
                    rev_mask);
            } else {
                ESP_LOGE(TAG, "❌ NVS commit 失败");
            }
        } else {
            ESP_LOGE(TAG, "❌ 保存失败: cal=%s mode=%s crsf=%s epa_p=%s epa_n=%s rev=%s",
                     esp_err_to_name(err_cal), esp_err_to_name(err_mode), esp_err_to_name(err_crsf),
                     esp_err_to_name(err_epa_pos), esp_err_to_name(err_epa_neg), esp_err_to_name(err_rev));
        }

        nvs_close(my_handle);
    } else {
        ESP_LOGE(TAG, "❌ NVS 打开失败，无法保存数据");
    }
}

// 开机时从 Flash 读取 limit 数组
void load_settings_from_nvs() {
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open("storage", NVS_READONLY, &my_handle);
    if (err == ESP_OK) {
        size_t required_size = sizeof(limit);
        esp_err_t err_cal = nvs_get_blob(my_handle, "cal_data", limit, &required_size);
        esp_err_t err_mode = nvs_get_u8(my_handle, "sim_mode", (uint8_t*)&current_sim_mode);
        uint8_t crsf_half = 0;
        esp_err_t err_crsf = nvs_get_u8(my_handle, "crsf_half", &crsf_half);

        // EPA/REV
        size_t epa_size = sizeof(epa_pos);
        nvs_get_blob(my_handle, "epa_pos", epa_pos, &epa_size);
        epa_size = sizeof(epa_neg);
        nvs_get_blob(my_handle, "epa_neg", epa_neg, &epa_size);
        nvs_get_u16(my_handle, "rev_mask", &rev_mask);

        // 预留字段
        size_t chmap_size = sizeof(ch_map);
        nvs_get_blob(my_handle, "ch_map", ch_map, &chmap_size);
        nvs_get_u8(my_handle, "stick_mode", &stick_mode);
        size_t btn_size = sizeof(btn_cfg);
        nvs_get_blob(my_handle, "btn_cfg", btn_cfg, &btn_size);

        if (err_cal == ESP_OK) {
            ESP_LOGI(TAG, "📂 从 Flash 成功加载校准数据！");
        } else {
            ESP_LOGI(TAG, "🆕 未找到校准数据，使用默认值。");
        }

        if (err_mode == ESP_OK) {
            ESP_LOGI(TAG, "📂 从 Flash 成功加载模式: %d", current_sim_mode);
        } else {
            current_sim_mode = SIM_MODE_DEFAULT;
            ESP_LOGI(TAG, "🆕 未找到模式数据，使用默认 HID 模式。");
        }

        if (err_crsf == ESP_OK && (crsf_half == 0 || crsf_half == 1)) {
            s_saved_crsf_half_duplex = (crsf_half == 1);
            s_has_saved_crsf_link_mode = true;
            ESP_LOGI(TAG, "📂 从 Flash 成功加载 CRSF 链路: %s", s_saved_crsf_half_duplex ? "single" : "dual");
        } else {
            s_saved_crsf_half_duplex = false;
            s_has_saved_crsf_link_mode = false;
            ESP_LOGI(TAG, "🆕 未找到 CRSF 链路配置，使用默认 dual。");
        }

        nvs_close(my_handle);
    } else {
        s_saved_crsf_half_duplex = false;
        s_has_saved_crsf_link_mode = false;
    }
}

// =================================================================================
// 处理前端网页请求 (/)
// =================================================================================
static esp_err_t index_html_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    size_t html_size = (index_html_end - index_html_start);
    httpd_resp_send(req, (const char *)index_html_start, html_size);
    return ESP_OK;
}

static esp_err_t favicon_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "204 No Content");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

// =================================================================================
// WebSocket 接收回调函数 (处理 GET_CAL 和 C: 保存指令)
// =================================================================================
static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG, "WebSocket 握手成功");
        return ESP_OK;
    }

    httpd_ws_frame_t ws_pkt;
    uint8_t *buf = NULL;
    memset(&ws_pkt, 0, sizeof(ws_pkt));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) return ret;

    if (ws_pkt.len > 0) {
        buf = calloc(1, ws_pkt.len + 1);
        if (buf == NULL) return ESP_ERR_NO_MEM;

        ws_pkt.payload = buf;
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);

        if (ret == ESP_OK) {
            char *text = (char *)ws_pkt.payload;

            // 1. 前端请求当前配置
            if (strncmp(text, "GET_CAL", 7) == 0) {
                ESP_LOGI(TAG, "收到前端 GET_CAL 请求，正在下发模式和配置...");

                // 先发模式
                char mode_buf[16];
                snprintf(mode_buf, sizeof(mode_buf), "M:%d\n", (int)current_sim_mode);

                httpd_ws_frame_t mode_pkt;
                memset(&mode_pkt, 0, sizeof(mode_pkt));
                mode_pkt.payload = (uint8_t*)mode_buf;
                mode_pkt.len = strlen(mode_buf);
                mode_pkt.type = HTTPD_WS_TYPE_TEXT;

                httpd_ws_send_frame(req, &mode_pkt);

                
               

                // 再发校准
                char cal_buf[512] = "C:";
                int offset = 2;

                for (int i = 0; i < 16; i++) {
                    int written = snprintf(
                        cal_buf + offset,
                        sizeof(cal_buf) - offset,
                        "%d,%d,%d,%d%s",
                        i + 1,
                        limit[i].raw_min,
                        limit[i].raw_mid,
                        limit[i].raw_max,
                        (i == 15) ? "\n" : ";"
                    );

                    if (written < 0 || written >= (int)(sizeof(cal_buf) - offset)) {
                        ESP_LOGE(TAG, "cal_buf 空间不足");
                        break;
                    }
                    offset += written;
                }

                httpd_ws_frame_t cal_pkt;
                memset(&cal_pkt, 0, sizeof(cal_pkt));
                cal_pkt.payload = (uint8_t*)cal_buf;
                cal_pkt.len = strlen(cal_buf);
                cal_pkt.type = HTTPD_WS_TYPE_TEXT;

                httpd_ws_send_frame(req, &cal_pkt);

                // 下发 EPA 数据 (EPA:ch,pos,neg;...)
                char epa_buf[384] = "EPA:";
                int epa_off = 4;
                for (int i = 0; i < 16; i++) {
                    int w = snprintf(epa_buf + epa_off, sizeof(epa_buf) - epa_off,
                                     "%d,%d,%d%s", i + 1, epa_pos[i], epa_neg[i],
                                     (i == 15) ? "\n" : ";");
                    if (w < 0 || w >= (int)(sizeof(epa_buf) - epa_off)) break;
                    epa_off += w;
                }
                httpd_ws_frame_t epa_pkt = { .type = HTTPD_WS_TYPE_TEXT, .payload = (uint8_t*)epa_buf, .len = strlen(epa_buf) };
                httpd_ws_send_frame(req, &epa_pkt);

                // 下发 REV 数据
                char rev_buf[32];
                snprintf(rev_buf, sizeof(rev_buf), "REV:%u\n", rev_mask);
                httpd_ws_frame_t rev_pkt = { .type = HTTPD_WS_TYPE_TEXT, .payload = (uint8_t*)rev_buf, .len = strlen(rev_buf) };
                httpd_ws_send_frame(req, &rev_pkt);

                // 下发 MAP 数据
                char map_buf[256] = "MAP:";
                int map_off = 4;
                for (int i = 0; i < 16; i++) {
                    int w = snprintf(map_buf + map_off, sizeof(map_buf) - map_off,
                                     "%d,%d%s", i + 1, ch_map[i],
                                     (i == 15) ? "\n" : ";");
                    if (w < 0 || w >= (int)(sizeof(map_buf) - map_off)) break;
                    map_off += w;
                }
                httpd_ws_frame_t map_pkt = { .type = HTTPD_WS_TYPE_TEXT, .payload = (uint8_t*)map_buf, .len = strlen(map_buf) };
                httpd_ws_send_frame(req, &map_pkt);

                // 下发 STICK_MODE
                char stick_buf[32];
                snprintf(stick_buf, sizeof(stick_buf), "STICK_MODE:%u\n", stick_mode);
                httpd_ws_frame_t stick_pkt = { .type = HTTPD_WS_TYPE_TEXT, .payload = (uint8_t*)stick_buf, .len = strlen(stick_buf) };
                httpd_ws_send_frame(req, &stick_pkt);

                // 下发 BTN (按键触发模式)
                char btn_buf[32];
                snprintf(btn_buf, sizeof(btn_buf), "BTN:%u,%u,%u,%u\n",
                         btn_cfg[0], btn_cfg[1], btn_cfg[2], btn_cfg[3]);
                httpd_ws_frame_t btn_pkt = { .type = HTTPD_WS_TYPE_TEXT, .payload = (uint8_t*)btn_buf, .len = strlen(btn_buf) };
                httpd_ws_send_frame(req, &btn_pkt);

                ws_send_crsf_snapshot(req);
                ESP_LOGI(TAG, "模式和配置下发完毕！");
            }

            // 2. 前端保存模式
            else if (strncmp(text, "M:", 2) == 0) {
                int mode = atoi(text + 2);
                if (mode == SIM_MODE_DEFAULT || mode == SIM_MODE_XBOX) {
                    portENTER_CRITICAL(&cfg_lock);
                    current_sim_mode = (sim_mode_t)mode;
                    portEXIT_CRITICAL(&cfg_lock);
                    request_nvs_save();
                    ESP_LOGI(TAG, "模式已更新为: %d", current_sim_mode);
                } else {
                    ESP_LOGW(TAG, "收到非法模式值: %d", mode);
                }
            }
    

            // 3. 前端保存校准
            else if (strncmp(text, "C:", 2) == 0) {
                ESP_LOGI(TAG, "收到前端保存指令，开始更新内存...");
                char *payload_data = text + 2;
                char *saveptr1;
                char *channel_str = strtok_r(payload_data, ";", &saveptr1);

                portENTER_CRITICAL(&cfg_lock);
                while (channel_str != NULL) {
                    int ch, min, mid, max;
                    if (sscanf(channel_str, "%d,%d,%d,%d", &ch, &min, &mid, &max) == 4) {
                        if (ch >= 1 && ch <= 16) {
                            limit[ch - 1].raw_min = min;
                            limit[ch - 1].raw_mid = mid;
                            limit[ch - 1].raw_max = max;
                        }
                    }
                    channel_str = strtok_r(NULL, ";", &saveptr1);
                }
                portEXIT_CRITICAL(&cfg_lock);

                request_nvs_save();
                ws_send_text(req, "CAL_OK");
                ESP_LOGI(TAG, "校准数据已保存 (RAM+NVS，即时生效)");
            }
            else if (strcmp(text, "SAVE_NVS") == 0) {
                request_nvs_save();
                ESP_LOGI(TAG, "NVS 已保存");
            }
            else if (strcmp(text, "CRSF_REFRESH") == 0) {
                ESP_LOGI(TAG, "网页命令 refresh: 请求刷新 CRSF 菜单");
                crsf_request_menu_reload();
                char status_buf[CRSF_STATUS_BUF_SIZE];
                build_crsf_status_payload(status_buf, sizeof(status_buf));
                ws_send_text(req, status_buf);
                ESP_LOGI(TAG, "已响应 CRSF_REFRESH，等待菜单重载完成后下发新快照");
            }
            else if (strcmp(text, "CRSF_SNAPSHOT") == 0) {
                ws_send_crsf_snapshot(req);
            }
            else if (strncmp(text, "CRSF_LINK:", 10) == 0) {
                const char *mode = text + 10;
                if (strcasecmp(mode, "SINGLE") == 0) {
                    ESP_LOGW(TAG, "网页命令 link: 切换为单线模式");
                    crsf_set_link_mode(true);
                    s_saved_crsf_half_duplex = true;
                    s_has_saved_crsf_link_mode = true;
                    request_nvs_save();
                    ws_send_crsf_snapshot(req);
                } else if (strcasecmp(mode, "DUAL") == 0) {
                    ESP_LOGW(TAG, "网页命令 link: 切换为双线模式");
                    crsf_set_link_mode(false);
                    s_saved_crsf_half_duplex = false;
                    s_has_saved_crsf_link_mode = true;
                    request_nvs_save();
                    ws_send_crsf_snapshot(req);
                } else {
                    ESP_LOGW(TAG, "非法 CRSF_LINK 指令: %s", text);
                }
            }
            else if (strncmp(text, "CRSF_WRITE:", 11) == 0) {
                int param_id = -1;
                int new_value = -1;
                if (sscanf(text + 11, "%d:%d", &param_id, &new_value) == 2 &&
                    param_id > 0 && param_id <= CRSF_MAX_MENU_ITEMS &&
                    new_value >= 0 && new_value <= 255) {
                    log_crsf_web_action("write", (uint8_t)param_id, new_value, true);
                    crsf_write_menu_value((uint8_t)param_id, (uint8_t)new_value);
                } else {
                    ESP_LOGW(TAG, "非法 CRSF_WRITE 指令: %s", text);
                }
            }
            else if (strncmp(text, "CRSF_BIND:", 10) == 0) {
                int param_id = -1;
                if (sscanf(text + 10, "%d", &param_id) == 1 &&
                    param_id > 0 && param_id <= CRSF_MAX_MENU_ITEMS) {
                    log_crsf_web_action("bind", (uint8_t)param_id, 1, false);
                    crsf_write_menu_value((uint8_t)param_id, 1);
                } else {
                    ESP_LOGW(TAG, "非法 CRSF_BIND 指令: %s", text);
                }
            }
            else if (strncmp(text, "CRSF_COMMAND:", 13) == 0) {
                int param_id = -1;
                if (sscanf(text + 13, "%d", &param_id) == 1 &&
                    param_id > 0 && param_id <= CRSF_MAX_MENU_ITEMS) {
                    log_crsf_web_action("command", (uint8_t)param_id, 1, false);
                    crsf_write_menu_value((uint8_t)param_id, 1);
                } else {
                    ESP_LOGW(TAG, "非法 CRSF_COMMAND 指令: %s", text);
                }
            }

            // ---- EPA/REV 命令 ----
            else if (strcmp(text, "EPA_READ") == 0) {
                char epa_buf[384] = "EPA:";
                int epa_off = 4;
                for (int i = 0; i < 16; i++) {
                    int w = snprintf(epa_buf + epa_off, sizeof(epa_buf) - epa_off,
                                     "%d,%d,%d%s", i + 1, epa_pos[i], epa_neg[i],
                                     (i == 15) ? "\n" : ";");
                    if (w < 0 || w >= (int)(sizeof(epa_buf) - epa_off)) break;
                    epa_off += w;
                }
                ws_send_text(req, epa_buf);
            }
            else if (strcmp(text, "REV_READ") == 0) {
                char rev_buf[32];
                snprintf(rev_buf, sizeof(rev_buf), "REV:%u\n", rev_mask);
                ws_send_text(req, rev_buf);
            }
            else if (strncmp(text, "EPA:", 4) == 0) {
                int ch, pos, neg;
                if (sscanf(text + 4, "%d,%d,%d", &ch, &pos, &neg) == 3 &&
                    ch >= 1 && ch <= 16 && pos >= 0 && pos <= 200 && neg >= 0 && neg <= 200) {
                    portENTER_CRITICAL(&cfg_lock);
                    epa_pos[ch - 1] = (uint8_t)pos;
                    epa_neg[ch - 1] = (uint8_t)neg;
                    portEXIT_CRITICAL(&cfg_lock);
                    request_nvs_save();
                    ESP_LOGI(TAG, "EPA CH%d: pos=%d neg=%d", ch, pos, neg);
                } else {
                    ESP_LOGW(TAG, "非法 EPA 指令: %s", text);
                }
            }
            else if (strncmp(text, "REV:", 4) == 0) {
                int ch, val;
                if (sscanf(text + 4, "%d,%d", &ch, &val) == 2 &&
                    ch >= 1 && ch <= 16 && (val == 0 || val == 1)) {
                    portENTER_CRITICAL(&cfg_lock);
                    if (val)
                        rev_mask |= (1 << (ch - 1));
                    else
                        rev_mask &= ~(1 << (ch - 1));
                    portEXIT_CRITICAL(&cfg_lock);
                    request_nvs_save();
                    ESP_LOGI(TAG, "REV CH%d: %s", ch, val ? "REV" : "NOR");
                } else {
                    ESP_LOGW(TAG, "非法 REV 指令: %s", text);
                }
            }

            // ---- MAP 通道映射 ----
            else if (strncmp(text, "MAP:", 4) == 0) {
                char *payload = text + 4;
                char *saveptr;
                char *segment = strtok_r(payload, ";", &saveptr);
                uint8_t new_map[16];
                memcpy(new_map, ch_map, sizeof(new_map));

                while (segment != NULL) {
                    int ch, src;
                    if (sscanf(segment, "%d,%d", &ch, &src) == 2 &&
                        ch >= 1 && ch <= 16) {
                        new_map[ch - 1] = (uint8_t)src;
                    } else {
                        // 尝试解析字符串源名
                        char src_str[8] = {0};
                        if (sscanf(segment, "%d,%7s", &ch, src_str) == 2 &&
                            ch >= 1 && ch <= 16) {
                            uint8_t src_idx = 0xFF;
                            if (strcasecmp(src_str, "SA") == 0) src_idx = 4;
                            else if (strcasecmp(src_str, "SB") == 0) src_idx = 5;
                            else if (strcasecmp(src_str, "SC") == 0) src_idx = 6;
                            else if (strcasecmp(src_str, "SD") == 0) src_idx = 7;
                            else if (strcasecmp(src_str, "NONE") == 0) src_idx = 0xFF;
                            if (src_idx != 0xFF || strcasecmp(src_str, "NONE") == 0)
                                new_map[ch - 1] = src_idx;
                        }
                    }
                    segment = strtok_r(NULL, ";", &saveptr);
                }

                portENTER_CRITICAL(&cfg_lock);
                memcpy(ch_map, new_map, sizeof(ch_map));
                portEXIT_CRITICAL(&cfg_lock);
                request_nvs_save();
                ws_send_text(req, "MAP_OK\n");
                ESP_LOGI(TAG, "MAP 已更新");
            }

            // ---- STICK_MODE ----
            else if (strncmp(text, "STICK_MODE:", 11) == 0) {
                int mode = atoi(text + 11);
                if (mode >= 1 && mode <= 4) {
                    portENTER_CRITICAL(&cfg_lock);
                    stick_mode = (uint8_t)mode;
                    portEXIT_CRITICAL(&cfg_lock);
                    request_nvs_save();
                    ESP_LOGI(TAG, "STICK_MODE 已更新为: %d", stick_mode);
                } else {
                    ESP_LOGW(TAG, "非法 STICK_MODE: %d", mode);
                }
            }

            // ---- BTN 按键触发模式 ----
            else if (strncmp(text, "BTN:", 4) == 0) {
                int sa, sb, sc, sd;
                if (sscanf(text + 4, "%d,%d,%d,%d", &sa, &sb, &sc, &sd) == 4 &&
                    sa >= 0 && sa <= 2 && sb >= 0 && sb <= 1 &&
                    sc >= 0 && sc <= 1 && sd >= 0 && sd <= 2) {
                    portENTER_CRITICAL(&cfg_lock);
                    btn_cfg[0] = (uint8_t)sa;
                    btn_cfg[1] = (uint8_t)sb;
                    btn_cfg[2] = (uint8_t)sc;
                    btn_cfg[3] = (uint8_t)sd;
                    portEXIT_CRITICAL(&cfg_lock);
                    request_nvs_save();
                    ESP_LOGI(TAG, "BTN 已更新: SA=%d SB=%d SC=%d SD=%d", sa, sb, sc, sd);
                } else {
                    ESP_LOGW(TAG, "非法 BTN 指令: %s", text);
                }
            }
        }

        free(buf);
    }

   

    return ret;
}



// =================================================================================
// WebSocket 主动推送任务 (发送展开后的 16 个通道真实数据)
// =================================================================================
static void ws_broadcast_task(void *arg)
{
    // 强制转换传进来的真实数据指针
    fpv_joystick_report_t *joy = (fpv_joystick_report_t *)arg; 
    
    char send_buf[512]; 
    char status_buf[CRSF_STATUS_BUF_SIZE];
    static char menu_buf[CRSF_MENU_BUF_SIZE];
    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;
    bool last_crsf_ready = false;
    bool last_crsf_linked = false;
    uint8_t last_loaded_params = 0xFF;
    uint8_t last_total_params = 0xFF;
    uint8_t last_rssi = 0xFF;
    uint8_t last_lq = 0xFF;
    int8_t last_snr = 0x7F;
    char last_device_name[64] = {0};
    uint8_t last_menu_loaded_params = 0xFF;
    uint8_t last_menu_total_params = 0xFF;

    // 增量发送 + 保活：数据变时立即发，最长 1.5s 不发就强制发一次防 TCP 断开
    char last_send_buf[512] = {0};
    int silent_cycles = 0;

    while (1) {
        if (server != NULL && joy != NULL) {

            snprintf(send_buf, sizeof(send_buf),
                "%d:%d,%d:%d,%d:%d,%d:%d,%d:%d,%d:%d,%d:%d,%d:%d," // CH1 ~ CH8
                "%d:%d,%d:%d,%d:%d,%d:%d,%d:%d,%d:%d,%d:%d,%d:%d\n", // CH9 ~ CH16

                joy->roll,     joy->raw_roll,
                joy->pitch,    joy->raw_pitch,
                joy->throttle, joy->raw_throttle,
                joy->yaw,      joy->raw_yaw,
                joy->aux1,     joy->raw_aux1,
                joy->aux2,     joy->raw_aux2,
                joy->aux3,     joy->raw_aux3,
                joy->aux4,     joy->raw_aux4,

                joy->sw1,     joy->sw1,
                joy->sw2,     joy->sw2,
                joy->sw3,     joy->sw3,
                joy->sw4,     joy->sw4,
                joy->sw5,     joy->sw5,
                joy->sw6,     joy->sw6,
                joy->sw7,     joy->sw7,
                joy->sw8,     joy->sw8
            );

            bool changed = (strcmp(send_buf, last_send_buf) != 0);

            if (changed || ++silent_cycles >= 30) {
                if (changed) silent_cycles = 0;
                memcpy(last_send_buf, send_buf, sizeof(last_send_buf));

                ws_pkt.payload = (uint8_t*)send_buf;
                ws_pkt.len = strlen(send_buf);

                size_t max_clients = 8;
                int client_fds[8] = {0};
                if (httpd_get_client_list(server, &max_clients, client_fds) == ESP_OK) {
                    for (int i = 0; i < max_clients; i++) {
                        if (httpd_ws_get_fd_info(server, client_fds[i]) == HTTPD_WS_CLIENT_WEBSOCKET) {
                            httpd_ws_send_frame_async(server, client_fds[i], &ws_pkt);
                        }
                    }
                }
            }
            crsf_state_t *state = crsf_get_state();
            if (crsf_status_needs_broadcast(
                    state,
                    &last_crsf_ready,
                    &last_crsf_linked,
                    &last_loaded_params,
                    &last_total_params,
                    &last_rssi,
                    &last_lq,
                    &last_snr,
                    last_device_name,
                    sizeof(last_device_name))) {
                build_crsf_status_payload(status_buf, sizeof(status_buf));
                ws_broadcast_text(status_buf);
            }

            if (state &&
                (state->loaded_params != last_menu_loaded_params ||
                 state->total_params != last_menu_total_params)) {
                last_menu_loaded_params = state->loaded_params;
                last_menu_total_params = state->total_params;

                if (state->total_params > 0 && state->loaded_params == state->total_params) {
                    size_t menu_payload_len = build_crsf_menu_payload(menu_buf, sizeof(menu_buf));
                    if (menu_payload_len > 0) {
                        ws_broadcast_text(menu_buf);
                        ESP_LOGI(TAG, "已推送 CRSF 菜单快照: %u/%u",
                                 state->loaded_params, state->total_params);
                    }
                }
            }
        }
        do_nvs_save();
        vTaskDelay(pdMS_TO_TICKS(50)); // 20Hz 刷新率
    }
}

// 404 → 204: 静默吃掉未知请求（App 后台请求不会重试）
static esp_err_t catchall_204_handler(httpd_req_t *req, httpd_err_code_t err)
{
    (void)err;
    httpd_resp_set_status(req, "204 No Content");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

// =================================================================================
// HTTP 服务器与路由注册
// =================================================================================
static void start_webserver(fpv_joystick_report_t *joy)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 10;
    config.max_open_sockets = 13;
    config.lru_purge_enable = true;

    ESP_LOGI(TAG, "启动 HTTP 服务器，端口: %d", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {

        httpd_uri_t index_uri = {
            .uri       = "/",
            .method    = HTTP_GET,
            .handler   = index_html_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &index_uri);

        httpd_uri_t icon_uri = {
            .uri       = "/logo-icon.png",
            .method    = HTTP_GET,
            .handler   = logo_icon_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &icon_uri);

        httpd_uri_t horiz_uri = {
            .uri       = "/logo-horizontal.png",
            .method    = HTTP_GET,
            .handler   = logo_horizontal_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &horiz_uri);

        // favicon.ico — 浏览器自动请求，直接回 204 静默处理
        httpd_uri_t favicon_uri = {
            .uri       = "/favicon.ico",
            .method    = HTTP_GET,
            .handler   = favicon_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &favicon_uri);

        // generate_204 — 手机连通性检测，回 204 即可避免弹窗和反复重试
        httpd_uri_t gen204_uri = {
            .uri       = "/generate_204",
            .method    = HTTP_GET,
            .handler   = favicon_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &gen204_uri);

        // 404→204 静默处理所有未知请求
        httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, catchall_204_handler);

        httpd_uri_t ws_uri = {
            .uri        = "/ws",
            .method     = HTTP_GET,
            .handler    = ws_handler,
            .user_ctx   = NULL,
            .is_websocket = true
        };
        httpd_register_uri_handler(server, &ws_uri);
        
        xTaskCreate(ws_broadcast_task, "ws_broadcast", 4096, joy, 5, NULL);
    }
}

// =================================================================================
// 初始化 WiFi SoftAP (热点) 并拉起服务器
// =================================================================================
void rc_wifi_server_init(fpv_joystick_report_t *joy)
{
    // ⭐ 在启动 WiFi 之前，先去 Flash 里捞一下保存过的数据
    

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = "DARWINFPV_WIFI",
            .ssid_len = strlen("DARWINFPV_WIFI"),
            .channel = 1,
            .password = "", 
            .max_connection = 4,
            .authmode = WIFI_AUTH_OPEN
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    // 提升 TCP 吞吐量: 关闭省电、增大带宽、延长 AP 超时
    esp_wifi_set_ps(WIFI_PS_NONE);
    esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_BW_HT40);
    esp_wifi_set_inactive_time(WIFI_IF_AP, 65535);

    ESP_LOGI(TAG, "WiFi AP 启动完成. SSID:%s", wifi_config.ap.ssid);

    xTaskCreatePinnedToCore(dns_server_task, "dns_captive", 3072, NULL, 3, NULL, 0);


    start_webserver(joy);
}
