#include <string.h>
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

static const char *TAG = "RC_WIFI";
static httpd_handle_t server = NULL;

// 1. 本地的 16 通道测试数组 (出厂默认值)
extern channel_cal_t limit[16] ;
extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[]   asm("_binary_index_html_end");

// =================================================================================
// ⭐ NVS 存储功能 (必须写在被调用之前)
// =================================================================================
// 把内存里的 limit 数组保存到 Flash
void save_settings_to_nvs(void) {
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &my_handle);
    if (err == ESP_OK) {
        esp_err_t err_cal = nvs_set_blob(my_handle, "cal_data", limit, sizeof(limit));
        esp_err_t err_mode = nvs_set_u8(my_handle, "sim_mode", (uint8_t)current_sim_mode);
        esp_err_t err_power = nvs_set_u8(my_handle, "crsf_power", crsf_get_power());

        if (err_cal == ESP_OK && err_mode == ESP_OK && err_power == ESP_OK) {
            esp_err_t err_commit = nvs_commit(my_handle);
            if (err_commit == ESP_OK) {
                ESP_LOGI(TAG, "💾 校准数据和模式已永久保存到 Flash (NVS)！");
            } else {
                ESP_LOGE(TAG, "❌ NVS commit 失败");
            }
        } else {
            ESP_LOGE(TAG, "❌ 保存失败: cal=%s, mode=%s, power=%s",
                     esp_err_to_name(err_cal), esp_err_to_name(err_mode), esp_err_to_name(err_power));
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
        uint8_t saved_power = CRSF_POWER_25MW;
        esp_err_t err_power = nvs_get_u8(my_handle, "crsf_power", &saved_power);

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

        if (err_power == ESP_OK) {
            crsf_set_power(saved_power);
            ESP_LOGI(TAG, "📂 从 Flash 成功加载 CRSF 功率: %u", saved_power);
        } else {
            crsf_set_power(CRSF_POWER_25MW);
            ESP_LOGI(TAG, "🆕 未找到 CRSF 功率数据，使用默认值。");
        }

        nvs_close(my_handle);
    } else {
        current_sim_mode = SIM_MODE_DEFAULT;
        crsf_set_power(CRSF_POWER_25MW);
        ESP_LOGI(TAG, "🆕 未找到保存数据，使用出厂默认值。");
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

            // 1. 前端请求当前配置
            if (strncmp((char*)ws_pkt.payload, "GET_CAL", 7) == 0) {
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

                char power_buf[16];
                snprintf(power_buf, sizeof(power_buf), "P:%u\n", crsf_get_power());

                httpd_ws_frame_t power_pkt;
                memset(&power_pkt, 0, sizeof(power_pkt));
                power_pkt.payload = (uint8_t*)power_buf;
                power_pkt.len = strlen(power_buf);
                power_pkt.type = HTTPD_WS_TYPE_TEXT;

                httpd_ws_send_frame(req, &power_pkt);

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
                ESP_LOGI(TAG, "模式和配置下发完毕！");
            }

            // 2. 前端保存模式
            else if (strncmp((char*)ws_pkt.payload, "M:", 2) == 0) {
                int mode = atoi((char*)ws_pkt.payload + 2);
                if (mode == SIM_MODE_DEFAULT || mode == SIM_MODE_XBOX) {
                    current_sim_mode = (sim_mode_t)mode;
                    save_settings_to_nvs();
                    ESP_LOGI(TAG, "模式已更新为: %d", current_sim_mode);
                } else {
                    ESP_LOGW(TAG, "收到非法模式值: %d", mode);
                }
            }
            else if (strncmp((char*)ws_pkt.payload, "P:", 2) == 0) {
                int power = atoi((char*)ws_pkt.payload + 2);
                if (power >= 0 && power < CRSF_POWER_COUNT) {
                    crsf_set_power((uint8_t)power);
                    save_settings_to_nvs();
                    ESP_LOGI(TAG, "CRSF 功率已更新为: %d", power);
                } else {
                    ESP_LOGW(TAG, "收到非法功率值: %d", power);
                }
            }
            else if (strncmp((char*)ws_pkt.payload, "BIND", 4) == 0) {
                crsf_request_bind();
                ESP_LOGI(TAG, "已请求 CRSF Bind");
            }

            // 3. 前端保存校准
            else if (strncmp((char*)ws_pkt.payload, "C:", 2) == 0) {
                ESP_LOGI(TAG, "收到前端保存指令，开始更新内存...");
                char *payload_data = (char*)ws_pkt.payload + 2;
                char *saveptr1;
                char *channel_str = strtok_r(payload_data, ";", &saveptr1);

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

                save_settings_to_nvs();
                ESP_LOGI(TAG, "校准数据已更新并保存");
                vTaskDelay(pdMS_TO_TICKS(300));
                ESP_LOGI(TAG, "正在重启设备...");
                esp_restart();
            }
        }

        free(buf);
    }

   

    return ret;
}

// static esp_err_t ws_handler(httpd_req_t *req)
// {
//     if (req->method == HTTP_GET) {
//         ESP_LOGI(TAG, "WebSocket 握手成功");
//         return ESP_OK;
//     }

//     httpd_ws_frame_t ws_pkt;
//     uint8_t *buf = NULL;
//     memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
//     ws_pkt.type = HTTPD_WS_TYPE_TEXT;

//     esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
//     if (ret != ESP_OK) return ret;

//     if (ws_pkt.len > 0) {
//         buf = calloc(1, ws_pkt.len + 1);
//         if (buf == NULL) return ESP_ERR_NO_MEM;
        
//         ws_pkt.payload = buf;
//         ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        
//         if (ret == ESP_OK) {
            
//             // ⭐ 分支 1：处理前端请求获取当前校准值 (GET_CAL)
//             if (strncmp((char*)ws_pkt.payload, "GET_CAL", 7) == 0) {
//                 ESP_LOGI(TAG, "收到前端 GET_CAL 请求，正在下发配置...");
                
//                 char cal_buf[512] = "C:"; 
//                 int offset = 2; // 跳过 "C:"
                
//                 for (int i = 0; i < 16; i++) {
//                     int written = snprintf(cal_buf + offset, sizeof(cal_buf) - offset, 
//                                            "%d,%d,%d,%d%s", 
//                                            i + 1, limit[i].raw_min, limit[i].raw_mid, limit[i].raw_max,
//                                            (i == 15) ? "\n" : ";");
//                     offset += written;
//                 }

//                 httpd_ws_frame_t out_pkt;
//                 memset(&out_pkt, 0, sizeof(httpd_ws_frame_t));
//                 out_pkt.payload = (uint8_t*)cal_buf;
//                 out_pkt.len = strlen(cal_buf);
//                 out_pkt.type = HTTPD_WS_TYPE_TEXT;
                
//                 httpd_ws_send_frame(req, &out_pkt);
//                 ESP_LOGI(TAG, "配置下发完毕！");
//             }
            
//             // ⭐ 分支 2：处理前端发来的保存指令 (C:)
//             else if (strncmp((char*)ws_pkt.payload, "C:", 2) == 0) {
//                 ESP_LOGI(TAG, "收到前端保存指令，开始更新内存...");
//                 char *payload_data = (char*)ws_pkt.payload + 2;
//                 char *saveptr1;
//                 char *channel_str = strtok_r(payload_data, ";", &saveptr1);
                
//                 while (channel_str != NULL) {
//                     int ch, min, mid, max;
//                     if (sscanf(channel_str, "%d,%d,%d,%d", &ch, &min, &mid, &max) == 4) {
//                         if (ch >= 1 && ch <= 16) {
//                             limit[ch - 1].raw_min = min;
//                             limit[ch - 1].raw_mid = mid;
//                             limit[ch - 1].raw_max = max;
//                         }
//                     }
//                     channel_str = strtok_r(NULL, ";", &saveptr1);
//                 }
                
//                 // 内存更新完后，触发一次 Flash 保存动作！
//                 save_settings_to_nvs();
//             }
//         }
//         free(buf);
//     }
//     return ret;
// }

// =================================================================================
// WebSocket 主动推送任务 (发送展开后的 16 个通道真实数据)
// =================================================================================
static void ws_broadcast_task(void *arg)
{
    // 强制转换传进来的真实数据指针
    fpv_joystick_report_t *joy = (fpv_joystick_report_t *)arg; 
    
    char send_buf[512]; 
    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    while (1) {
        // 确保 server 开启，并且 joy 指针不为空
        if (server != NULL && joy != NULL) {
            
            // 用一个巨大的 snprintf 将真实数据一次性填入！
            // 格式要求：前8个是 "映射:原始"，后8个数字开关没有物理原始值，直接发 "映射:0"
            snprintf(send_buf, sizeof(send_buf), 
                "%d:%d,%d:%d,%d:%d,%d:%d,%d:%d,%d:%d,%d:%d,%d:%d," // CH1 ~ CH8 (带原始值)
                "%d:%d,%d:%d,%d:%d,%d:%d,%d:%d,%d:%d,%d:%d,%d:%d\n",       // CH9 ~ CH16 (无原始值，补0)
                
                // 填入前 8 个模拟通道 (映射值, 原始值)
                joy->roll,     joy->raw_roll,
                joy->pitch,    joy->raw_pitch,
                joy->throttle, joy->raw_throttle,
                joy->yaw,      joy->raw_yaw,
                joy->aux1,     joy->raw_aux1,
                joy->aux2,     joy->raw_aux2,
                joy->aux3,     joy->raw_aux3,
                joy->aux4,     joy->raw_aux4,
                
                // 填入后 8 个开关通道 (只有映射值)
                joy->sw1,     joy->sw1, 
                joy->sw2,     joy->sw2, 
                joy->sw3,     joy->sw3, 
                joy->sw4,     joy->sw4, 
                joy->sw5,     joy->sw5, 
                joy->sw6,     joy->sw6, 
                joy->sw7,     joy->sw7, 
                joy->sw8,     joy->sw8
            );

            ws_pkt.payload = (uint8_t*)send_buf;
            ws_pkt.len = strlen(send_buf);

            size_t max_clients = 4;
            int client_fds[4];
            if (httpd_get_client_list(server, &max_clients, client_fds) == ESP_OK) {
                for (int i = 0; i < max_clients; i++) {
                    httpd_ws_send_frame_async(server, client_fds[i], &ws_pkt);
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50)); // 20Hz 刷新率
    }
}

// =================================================================================
// HTTP 服务器与路由注册
// =================================================================================
static void start_webserver(fpv_joystick_report_t *joy)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 10;
    
    ESP_LOGI(TAG, "启动 HTTP 服务器，端口: %d", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        
        httpd_uri_t index_uri = {
            .uri       = "/",
            .method    = HTTP_GET,
            .handler   = index_html_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &index_uri);
        
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

    ESP_LOGI(TAG, "WiFi AP 启动完成. SSID:%s", wifi_config.ap.ssid);
    
    start_webserver(joy);
}