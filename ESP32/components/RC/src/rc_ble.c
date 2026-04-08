#include <string.h>
#include <inttypes.h>
#include <assert.h>
#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs_adv.h"
#include "host/ble_store.h"
#include "host/ble_sm.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "services/dis/ble_svc_dis.h"
#include "os/os_mbuf.h"
#include "rc_ble.h"

void ble_store_config_init(void);

#define BLE_HID_APPEARANCE_GAMEPAD      0x03C4
#define BLE_HID_INFO_FLAGS              0x03
#define BLE_HID_COUNTRY_CODE            0x00
#define BLE_HID_VERSION                 0x0111
#define BLE_HID_REPORT_ID               0x01
#define BLE_HID_REPORT_TYPE_INPUT       0x01
#define BLE_HID_PROTOCOL_MODE_REPORT    0x01
#define BLE_HID_MAX_BONDS               4
#define BLE_HID_DEVICE_INFO_PNP_LEN     7

typedef struct __attribute__((packed)) {
    uint16_t x;
    uint16_t y;
    uint16_t z;
    uint16_t rz;
    uint8_t buttons;
} ble_gamepad_report_t;

typedef struct __attribute__((packed)) {
    uint16_t bcd_hid;
    uint8_t country_code;
    uint8_t flags;
} ble_hid_info_t;

typedef struct __attribute__((packed)) {
    uint8_t report_id;
    uint8_t report_type;
} ble_hid_report_ref_t;

static const char *TAG = "RC_BLE";
static QueueHandle_t s_input_queue = NULL;
static TaskHandle_t s_ble_send_task_handle = NULL;
static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_report_chr_val_handle = 0;
static bool s_connected = false;
static bool s_paired = false;
static bool s_subscribed = false;
static bool s_ready_to_send = false;
static uint8_t s_own_addr_type = BLE_OWN_ADDR_PUBLIC;
static uint32_t s_last_notify_tick = 0;
static fpv_joystick_report_t s_latest_joy;
static const ble_hid_info_t s_hid_info = {
    .bcd_hid = BLE_HID_VERSION,
    .country_code = BLE_HID_COUNTRY_CODE,
    .flags = BLE_HID_INFO_FLAGS,
};
static uint8_t s_protocol_mode = BLE_HID_PROTOCOL_MODE_REPORT;
static uint8_t s_control_point = 0;
static const ble_hid_report_ref_t s_input_report_ref = {
    .report_id = BLE_HID_REPORT_ID,
    .report_type = BLE_HID_REPORT_TYPE_INPUT,
};
static uint8_t s_pnp_id[BLE_HID_DEVICE_INFO_PNP_LEN] = {
    0x02,
    0x3A, 0x30,
    0x01, 0x40,
    0x00, 0x01,
};

static const uint8_t s_report_map[] = {
    0x05, 0x01,
    0x09, 0x05,
    0xA1, 0x01,
    0x85, 0x01,
    0x16, 0xE8, 0x03,
    0x26, 0xD0, 0x07,
    0x35, 0x00,
    0x46, 0xD0, 0x07,
    0x75, 0x10,
    0x95, 0x04,
    0x09, 0x30,
    0x09, 0x31,
    0x09, 0x32,
    0x09, 0x35,
    0x81, 0x02,
    0x05, 0x09,
    0x19, 0x01,
    0x29, 0x08,
    0x15, 0x00,
    0x25, 0x01,
    0x75, 0x01,
    0x95, 0x08,
    0x81, 0x02,
    0xC0,
};

static const ble_uuid16_t s_dis_service_uuid = BLE_UUID16_INIT(0x180A);
static const ble_uuid16_t s_hid_service_uuid = BLE_UUID16_INIT(0x1812);
static const ble_uuid16_t s_manufacturer_uuid = BLE_UUID16_INIT(0x2A29);
static const ble_uuid16_t s_pnp_id_uuid = BLE_UUID16_INIT(0x2A50);
static const ble_uuid16_t s_hid_info_uuid = BLE_UUID16_INIT(0x2A4A);
static const ble_uuid16_t s_report_map_uuid = BLE_UUID16_INIT(0x2A4B);
static const ble_uuid16_t s_hid_control_uuid = BLE_UUID16_INIT(0x2A4C);
static const ble_uuid16_t s_report_uuid = BLE_UUID16_INIT(0x2A4D);
static const ble_uuid16_t s_protocol_mode_uuid = BLE_UUID16_INIT(0x2A4E);
static const ble_uuid16_t s_report_ref_uuid = BLE_UUID16_INIT(0x2908);

static uint16_t ble_clamp_channel(uint16_t value)
{
    if (value < 1000) {
        return 1000;
    }
    if (value > 2000) {
        return 2000;
    }
    return value;
}

static void build_ble_gamepad_report(const fpv_joystick_report_t *joy, ble_gamepad_report_t *report)
{
    report->x = ble_clamp_channel(joy->roll);
    report->y = ble_clamp_channel(joy->pitch);
    report->z = ble_clamp_channel(joy->throttle);
    report->rz = ble_clamp_channel(joy->yaw);
    report->buttons = 0;

    if (joy->sw1 > 1500) report->buttons |= (1 << 0);
    if (joy->sw2 > 1500) report->buttons |= (1 << 1);
    if (joy->sw3 > 1500) report->buttons |= (1 << 2);
    if (joy->sw4 > 1500) report->buttons |= (1 << 3);
    if (joy->sw5 > 1500) report->buttons |= (1 << 4);
    if (joy->sw6 > 1500) report->buttons |= (1 << 5);
    if (joy->sw7 > 1500) report->buttons |= (1 << 6);
    if (joy->sw8 > 1500) report->buttons |= (1 << 7);
}

static bool ble_link_is_secured(const struct ble_gap_conn_desc *desc)
{
    return desc->sec_state.encrypted;
}

static void ble_update_ready_state(void)
{
    struct ble_gap_conn_desc desc;

    s_ready_to_send = false;
    s_paired = false;

    if (!s_connected || !s_subscribed || s_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        ESP_LOGI(TAG, "BLE ready=false connected=%d subscribed=%d handle=%u",
            s_connected, s_subscribed, s_conn_handle);
        return;
    }

    if (ble_gap_conn_find(s_conn_handle, &desc) != 0) {
        ESP_LOGW(TAG, "BLE ready=false conn lookup failed");
        return;
    }

    s_paired = desc.sec_state.bonded;
    s_ready_to_send = ble_link_is_secured(&desc);
    ESP_LOGI(TAG, "BLE ready=%d encrypted=%d bonded=%d subscribed=%d authenticated=%d",
        s_ready_to_send,
        desc.sec_state.encrypted,
        desc.sec_state.bonded,
        s_subscribed,
        desc.sec_state.authenticated);
}

static int ble_append_bytes(struct os_mbuf *om, const void *data, size_t len)
{
    return os_mbuf_append(om, data, len) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static int ble_report_map_access(uint16_t conn_handle, uint16_t attr_handle,
    struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;

    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    return ble_append_bytes(ctxt->om, s_report_map, sizeof(s_report_map));
}

static int ble_hid_info_access(uint16_t conn_handle, uint16_t attr_handle,
    struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;

    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    return ble_append_bytes(ctxt->om, &s_hid_info, sizeof(s_hid_info));
}

static int ble_protocol_mode_access(uint16_t conn_handle, uint16_t attr_handle,
    struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;

    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        return ble_append_bytes(ctxt->om, &s_protocol_mode, sizeof(s_protocol_mode));
    }

    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        if (ctxt->om->om_len != 1) {
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }
        s_protocol_mode = ctxt->om->om_data[0];
        return 0;
    }

    return BLE_ATT_ERR_UNLIKELY;
}

static int ble_input_report_access(uint16_t conn_handle, uint16_t attr_handle,
    struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    ble_gamepad_report_t report;

    (void)conn_handle;
    (void)attr_handle;
    (void)arg;

    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    build_ble_gamepad_report(&s_latest_joy, &report);
    return ble_append_bytes(ctxt->om, &report, sizeof(report));
}

static int ble_hid_control_access(uint16_t conn_handle, uint16_t attr_handle,
    struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;

    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        return ble_append_bytes(ctxt->om, &s_control_point, sizeof(s_control_point));
    }

    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        if (ctxt->om->om_len != 1) {
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }
        s_control_point = ctxt->om->om_data[0];
        return 0;
    }

    return BLE_ATT_ERR_UNLIKELY;
}

static int ble_manufacturer_access(uint16_t conn_handle, uint16_t attr_handle,
    struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    static const char manufacturer[] = "DARWINFPV";

    (void)conn_handle;
    (void)attr_handle;
    (void)arg;

    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    return ble_append_bytes(ctxt->om, manufacturer, sizeof(manufacturer) - 1);
}

static int ble_pnp_id_access(uint16_t conn_handle, uint16_t attr_handle,
    struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;

    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    return ble_append_bytes(ctxt->om, s_pnp_id, sizeof(s_pnp_id));
}

static int ble_report_ref_descriptor_access(uint16_t conn_handle, uint16_t attr_handle,
    struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;

    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_DSC) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    return ble_append_bytes(ctxt->om, &s_input_report_ref, sizeof(s_input_report_ref));
}

static int ble_gap_event(struct ble_gap_event *event, void *arg);

static struct ble_gatt_dsc_def s_input_report_descriptors[] = {
    {
        .uuid = &s_report_ref_uuid.u,
        .att_flags = BLE_ATT_F_READ,
        .access_cb = ble_report_ref_descriptor_access,
    },
    {0},
};

static const struct ble_gatt_chr_def s_dis_characteristics[] = {
    {
        .uuid = &s_manufacturer_uuid.u,
        .access_cb = ble_manufacturer_access,
        .flags = BLE_GATT_CHR_F_READ,
    },
    {
        .uuid = &s_pnp_id_uuid.u,
        .access_cb = ble_pnp_id_access,
        .flags = BLE_GATT_CHR_F_READ,
    },
    {0},
};

static const struct ble_gatt_chr_def s_hid_characteristics[] = {
    {
        .uuid = &s_protocol_mode_uuid.u,
        .access_cb = ble_protocol_mode_access,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE_NO_RSP,
    },
    {
        .uuid = &s_report_map_uuid.u,
        .access_cb = ble_report_map_access,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC,
    },
    {
        .uuid = &s_hid_info_uuid.u,
        .access_cb = ble_hid_info_access,
        .flags = BLE_GATT_CHR_F_READ,
    },
    {
        .uuid = &s_report_uuid.u,
        .access_cb = ble_input_report_access,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC | BLE_GATT_CHR_F_NOTIFY,
        .val_handle = &s_report_chr_val_handle,
        .descriptors = s_input_report_descriptors,
    },
    {
        .uuid = &s_hid_control_uuid.u,
        .access_cb = ble_hid_control_access,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE_NO_RSP,
    },
    {0},
};

static const struct ble_gatt_svc_def s_gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_dis_service_uuid.u,
        .characteristics = s_dis_characteristics,
    },
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_hid_service_uuid.u,
        .characteristics = s_hid_characteristics,
    },
    {0},
};

static void ble_start_advertising(void)
{
    struct ble_hs_adv_fields fields = {0};
    struct ble_gap_adv_params params = {0};
    ble_uuid16_t hid_uuid = BLE_UUID16_INIT(0x1812);
    const char *name = BLE_HID_DEVICE_NAME;
    int rc;

    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids16 = &hid_uuid;
    fields.num_uuids16 = 1;
    fields.uuids16_is_complete = 1;
    fields.appearance = BLE_HID_APPEARANCE_GAMEPAD;
    fields.appearance_is_present = 1;
    fields.name = (uint8_t *)name;
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to set advertising data: %d", rc);
        return;
    }

    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    params.itvl_min = BLE_GAP_ADV_ITVL_MS(40);
    params.itvl_max = BLE_GAP_ADV_ITVL_MS(60);

    rc = ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER, &params, ble_gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to start advertising: %d", rc);
        return;
    }

    ESP_LOGI(TAG, "BLE advertising started");
}

static int ble_gap_event(struct ble_gap_event *event, void *arg)
{
    struct ble_gap_conn_desc desc;

    (void)arg;

    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status != 0) {
                ESP_LOGW(TAG, "BLE connect failed: %d", event->connect.status);
                s_connected = false;
                s_subscribed = false;
                s_ready_to_send = false;
                s_paired = false;
                s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
                ble_start_advertising();
                return 0;
            }

            s_conn_handle = event->connect.conn_handle;
            s_connected = true;
            s_subscribed = false;
            ble_update_ready_state();
            ESP_LOGI(TAG, "BLE connected");

            if (ble_gap_conn_find(s_conn_handle, &desc) == 0) {
                ESP_LOGI(TAG, "BLE conn encrypted=%d bonded=%d authenticated=%d",
                    desc.sec_state.encrypted,
                    desc.sec_state.bonded,
                    desc.sec_state.authenticated);
            }
            return 0;

        case BLE_GAP_EVENT_DISCONNECT:
            s_connected = false;
            s_subscribed = false;
            s_ready_to_send = false;
            s_paired = false;
            s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
            ESP_LOGI(TAG, "BLE disconnected: %d", event->disconnect.reason);
            ble_start_advertising();
            return 0;

        case BLE_GAP_EVENT_ADV_COMPLETE:
            ble_start_advertising();
            return 0;

        case BLE_GAP_EVENT_SUBSCRIBE:
            if (event->subscribe.attr_handle == s_report_chr_val_handle) {
                s_subscribed = event->subscribe.cur_notify;
                ble_update_ready_state();
                ESP_LOGI(TAG, "BLE input notify %s", s_subscribed ? "enabled" : "disabled");
            }
            return 0;

        case BLE_GAP_EVENT_ENC_CHANGE:
            if (event->enc_change.status != 0) {
                ESP_LOGW(TAG, "BLE encryption failed: %d", event->enc_change.status);
            }
            ble_update_ready_state();
            if (ble_gap_conn_find(event->enc_change.conn_handle, &desc) == 0) {
                ESP_LOGI(TAG, "BLE security encrypted=%d bonded=%d authenticated=%d",
                    desc.sec_state.encrypted,
                    desc.sec_state.bonded,
                    desc.sec_state.authenticated);
            }
            return 0;

        case BLE_GAP_EVENT_REPEAT_PAIRING:
            if (ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc) == 0) {
                ble_store_util_delete_peer(&desc.peer_id_addr);
            }
            return BLE_GAP_REPEAT_PAIRING_RETRY;

        case BLE_GAP_EVENT_PASSKEY_ACTION:
            ESP_LOGW(TAG, "BLE passkey action: %d", event->passkey.params.action);
            if (event->passkey.params.action == BLE_SM_IOACT_INPUT) {
                struct ble_sm_io pkey = {0};
                pkey.action = event->passkey.params.action;
                pkey.passkey = 123456;
                ble_sm_inject_io(event->passkey.conn_handle, &pkey);
            }
            return 0;

        case BLE_GAP_EVENT_MTU:
            ESP_LOGI(TAG, "BLE mtu updated: %d", event->mtu.value);
            return 0;

        default:
            return 0;
    }
}

static void ble_on_reset(int reason)
{
    ESP_LOGW(TAG, "BLE stack reset: %d", reason);
}

static void ble_on_sync(void)
{
    int rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "BLE infer addr type failed: %d", rc);
        return;
    }

    ble_start_advertising();
}

static void ble_host_task(void *param)
{
    (void)param;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static void ble_send_task(void *arg)
{
    ble_gamepad_report_t report;
    fpv_joystick_report_t joy;
    uint32_t last_trace_tick = 0;

    (void)arg;

    while (1) {
        if (xQueueReceive(s_input_queue, &joy, pdMS_TO_TICKS(BLE_HID_TASK_PERIOD_MS)) != pdTRUE) {
            continue;
        }

        s_latest_joy = joy;

        if (!s_ready_to_send || s_conn_handle == BLE_HS_CONN_HANDLE_NONE || s_report_chr_val_handle == 0) {
            continue;
        }

        uint32_t now = xTaskGetTickCount();
        if ((now - s_last_notify_tick) < pdMS_TO_TICKS(BLE_HID_TASK_PERIOD_MS)) {
            continue;
        }
        s_last_notify_tick = now;

        build_ble_gamepad_report(&joy, &report);
        struct os_mbuf *om = ble_hs_mbuf_from_flat(&report, sizeof(report));
        if (om == NULL) {
            continue;
        }

        int rc = ble_gatts_notify_custom(s_conn_handle, s_report_chr_val_handle, om);
        if (rc != 0 && rc != BLE_HS_EDONE && rc != BLE_HS_ENOTCONN && rc != BLE_HS_EBUSY) {
            ESP_LOGW(TAG, "BLE notify failed: %d", rc);
        } else if ((now - last_trace_tick) >= pdMS_TO_TICKS(500)) {
            last_trace_tick = now;
            ESP_LOGI(TAG, "BLE notify ok x=%u y=%u z=%u rz=%u buttons=0x%02X",
                report.x, report.y, report.z, report.rz, report.buttons);
        }

        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

void ble_update_input(const fpv_joystick_report_t *joy)
{
    if (joy == NULL || s_input_queue == NULL) {
        return;
    }

    s_latest_joy = *joy;
    xQueueOverwrite(s_input_queue, joy);
}

void ble_init(fpv_joystick_report_t *joy)
{
    esp_err_t ret;
    int rc;

    s_connected = false;
    s_paired = false;
    s_subscribed = false;
    s_ready_to_send = false;
    s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
    s_report_chr_val_handle = 0;
    s_latest_joy = *joy;

    if (s_input_queue == NULL) {
        s_input_queue = xQueueCreate(1, sizeof(fpv_joystick_report_t));
        configASSERT(s_input_queue != NULL);
    }
    ble_update_input(joy);

    // ret = nvs_flash_init();
    // if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    //     ESP_ERROR_CHECK(nvs_flash_erase());
    //     ret = nvs_flash_init();
    // }
    // ESP_ERROR_CHECK(ret);

    ret = nimble_port_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(ret);
    }

    ble_hs_cfg.reset_cb = ble_on_reset;
    ble_hs_cfg.sync_cb = ble_on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

    ble_store_config_init();
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_svc_dis_init();
    ble_svc_gap_device_name_set(BLE_HID_DEVICE_NAME);
    ble_svc_dis_manufacturer_name_set("DARWINFPV");
    ble_svc_dis_model_number_set("FPV RC BLE V2");
    ble_svc_dis_serial_number_set("000001");
    ble_svc_dis_pnp_id_set((char *)s_pnp_id);

    rc = ble_gatts_count_cfg(s_gatt_svcs);
    ESP_ERROR_CHECK(rc);
    rc = ble_gatts_add_svcs(s_gatt_svcs);
    ESP_ERROR_CHECK(rc);

    if (s_ble_send_task_handle == NULL) {
        xTaskCreatePinnedToCore(ble_send_task, "ble_hid", BLE_HID_SEND_TASK_STACK, NULL,
            BLE_HID_TASK_PRIORITY, &s_ble_send_task_handle, tskNO_AFFINITY);
    }

    nimble_port_freertos_init(ble_host_task);
    ESP_LOGI(TAG, "BLE HID initialized with NimBLE");
}

bool ble_is_connected(void)
{
    return s_connected;
}

bool ble_is_paired(void)
{
    return s_paired;
}
