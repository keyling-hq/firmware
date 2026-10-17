#include <string.h>
#include "ble_link.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const char *TAG = "ble_link";

#define ADV_ITVL_MIN    BLE_GAP_ADV_ITVL_MS(100)
#define ADV_ITVL_MAX    BLE_GAP_ADV_ITVL_MS(150)
#define PASSKEY_MAX     1000000

void ble_store_config_init(void);

/* Nordic UART Service, so generic BLE terminal apps can talk to the dongle. */
static const ble_uuid128_t NUS_SVC_UUID =
    BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, 0x93, 0xf3, 0xa3, 0xb5, 0x01, 0x00, 0x40, 0x6e);
static const ble_uuid128_t NUS_RX_UUID =
    BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, 0x93, 0xf3, 0xa3, 0xb5, 0x02, 0x00, 0x40, 0x6e);
static const ble_uuid128_t NUS_TX_UUID =
    BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, 0x93, 0xf3, 0xa3, 0xb5, 0x03, 0x00, 0x40, 0x6e);

/* Bridge service: 7a1f0001-3c4e-4d8a-9b2e-5f6a7b8c9d0e, characteristics 0002 CMD, 0003 EVT, 0004 CTRL. */
#define BRIDGE_UUID(n) \
    BLE_UUID128_INIT(0x0e, 0x9d, 0x8c, 0x7b, 0x6a, 0x5f, 0x2e, 0x9b, 0x8a, 0x4d, 0x4e, 0x3c, (n), 0x00, 0x1f, 0x7a)

static const ble_uuid128_t BRIDGE_SVC_UUID = BRIDGE_UUID(0x01);
static const ble_uuid128_t BRIDGE_CMD_UUID = BRIDGE_UUID(0x02);
static const ble_uuid128_t BRIDGE_EVT_UUID = BRIDGE_UUID(0x03);
static const ble_uuid128_t BRIDGE_CTRL_UUID = BRIDGE_UUID(0x04);

#define NOTIFY_RETRY_MS     5
#define NOTIFY_RETRIES      200

static uint16_t s_cmd_handle;
static uint16_t s_evt_handle;
static uint16_t s_ctrl_handle;
static volatile bool s_evt_subscribed;
static bool s_bridge_session;

static ble_link_callbacks_t s_cb;
static void *s_cb_ctx;
static uint8_t s_own_addr_type;
static uint16_t s_tx_handle;
static uint16_t s_conn = BLE_HS_CONN_HANDLE_NONE;
static volatile bool s_subscribed;
static volatile bool s_passkey_shown;
static volatile bool s_window_open;
static volatile ble_link_state_t s_state = BLE_LINK_ADVERTISING;
static esp_timer_handle_t s_window_timer;
static uint8_t s_rx_buf[BLE_ATT_ATTR_MAX_LEN];

static void advertise(void);

static bool is_bonded(void)
{
    int count = 0;
    ble_store_util_count(BLE_STORE_OBJ_TYPE_PEER_SEC, &count);
    return count > 0;
}

static bool pairing_allowed(void)
{
    return s_window_open || !is_bonded();
}

static void report(void)
{
    if (s_cb.status) {
        const ble_link_status_t status = {
            .state = s_state,
            .bonded = is_bonded(),
            .pairing_window = s_window_open,
        };
        s_cb.status(&status, s_cb_ctx);
    }
}

static void hide_passkey(void)
{
    if (s_passkey_shown) {
        s_passkey_shown = false;
        if (s_cb.passkey) {
            s_cb.passkey(0, false, s_cb_ctx);
        }
    }
}

static int nus_access_cb(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    uint16_t len = 0;
    if (ble_hs_mbuf_to_flat(ctxt->om, s_rx_buf, sizeof(s_rx_buf), &len) != 0) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    if (s_cb.rx && s_state == BLE_LINK_CONNECTED) {
        s_cb.rx(s_rx_buf, len, s_cb_ctx);
    }
    return 0;
}

static void update_bridge_session(void)
{
    bool active = s_state == BLE_LINK_CONNECTED && s_evt_subscribed;
    if (active != s_bridge_session) {
        s_bridge_session = active;
        ESP_LOGI(TAG, "bridge session %s", active ? "started" : "ended");
        if (s_cb.bridge_session) {
            s_cb.bridge_session(active, s_cb_ctx);
        }
    }
}

static int bridge_access_cb(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    if (s_state != BLE_LINK_CONNECTED) {
        return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
    }
    if (attr == s_ctrl_handle) {
        if (s_cb.bridge_abort) {
            s_cb.bridge_abort(s_cb_ctx);
        }
        return 0;
    }
    uint16_t len = 0;
    if (ble_hs_mbuf_to_flat(ctxt->om, s_rx_buf, sizeof(s_rx_buf), &len) != 0) {
        ESP_LOGW(TAG, "CMD write of %u bytes rejected, max %u", (unsigned)OS_MBUF_PKTLEN(ctxt->om),
                 (unsigned)sizeof(s_rx_buf));
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    if (s_cb.bridge_rx) {
        s_cb.bridge_rx(s_rx_buf, len, s_cb_ctx);
    }
    return 0;
}

#define SECURE_WRITE (BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP | \
                      BLE_GATT_CHR_F_WRITE_ENC | BLE_GATT_CHR_F_WRITE_AUTHEN)

static const struct ble_gatt_svc_def s_services[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &BRIDGE_SVC_UUID.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &BRIDGE_CMD_UUID.u,
                .access_cb = bridge_access_cb,
                .val_handle = &s_cmd_handle,
                .flags = SECURE_WRITE,
            },
            {
                .uuid = &BRIDGE_EVT_UUID.u,
                .access_cb = bridge_access_cb,
                .val_handle = &s_evt_handle,
                .flags = BLE_GATT_CHR_F_NOTIFY,
            },
            {
                .uuid = &BRIDGE_CTRL_UUID.u,
                .access_cb = bridge_access_cb,
                .val_handle = &s_ctrl_handle,
                .flags = SECURE_WRITE,
            },
            { 0 },
        },
    },
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &NUS_SVC_UUID.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &NUS_RX_UUID.u,
                .access_cb = nus_access_cb,
                .flags = SECURE_WRITE,
            },
            {
                .uuid = &NUS_TX_UUID.u,
                .access_cb = nus_access_cb,
                .val_handle = &s_tx_handle,
                .flags = BLE_GATT_CHR_F_NOTIFY,
            },
            { 0 },
        },
    },
    { 0 },
};

/* Single owner: drop every bond except the one for keep (NULL drops all). Returns how many went. */
static int delete_other_bonds(const ble_addr_t *keep)
{
    ble_addr_t peers[CONFIG_BT_NIMBLE_MAX_BONDS];
    int count = 0;
    ble_store_util_bonded_peers(peers, &count, CONFIG_BT_NIMBLE_MAX_BONDS);

    int deleted = 0;
    for (int i = 0; i < count; i++) {
        if (!keep || ble_addr_cmp(&peers[i], keep) != 0) {
            ble_store_util_delete_peer(&peers[i]);
            deleted++;
        }
    }
    return deleted;
}

/*
 * The default policy (ble_store_util_status_rr) unpairs the oldest peer when the store is full,
 * which also drops its connection: that can be the phone that is pairing right now.
 */
static int store_status_cb(struct ble_store_status_event *event, void *arg)
{
    if (event->event_code != BLE_STORE_EVENT_OVERFLOW && event->event_code != BLE_STORE_EVENT_FULL) {
        return ble_store_util_status_rr(event, arg);
    }
    struct ble_gap_conn_desc desc;
    bool connected = s_conn != BLE_HS_CONN_HANDLE_NONE && ble_gap_conn_find(s_conn, &desc) == 0;
    int deleted = delete_other_bonds(connected ? &desc.peer_id_addr : NULL);
    ESP_LOGW(TAG, "bond store full (event %d): deleted %d previous bond(s)", event->event_code, deleted);
    return deleted ? 0 : BLE_HS_ESTORE_CAP;
}

static void on_secured(uint16_t conn)
{
    struct ble_gap_conn_desc desc;
    if (ble_gap_conn_find(conn, &desc) != 0) {
        return;
    }
    ESP_LOGI(TAG, "link secured: encrypted=%d authenticated=%d bonded=%d key_size=%d",
             desc.sec_state.encrypted, desc.sec_state.authenticated, desc.sec_state.bonded,
             desc.sec_state.key_size);
    if (!desc.sec_state.encrypted || !desc.sec_state.authenticated || !desc.sec_state.bonded) {
        ESP_LOGW(TAG, "link not authenticated and bonded, dropping it");
        ble_gap_terminate(conn, BLE_ERR_AUTH_FAIL);
        return;
    }
    if (s_passkey_shown) {
        int replaced = delete_other_bonds(&desc.peer_id_addr);
        ESP_LOGI(TAG, "paired, replaced %d previous bond(s)", replaced);
        hide_passkey();
        ble_link_pairing_window(false);
    }
    s_state = BLE_LINK_CONNECTED;
    report();
    update_bridge_session();
}

static int gap_event_cb(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status != 0) {
            advertise();
            return 0;
        }
        ESP_LOGI(TAG, "connected, bonded=%d", is_bonded());
        s_conn = event->connect.conn_handle;
        s_state = BLE_LINK_SECURING;
        report();
        ble_gap_security_initiate(s_conn);
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "disconnected, reason 0x%x", event->disconnect.reason);
        s_conn = BLE_HS_CONN_HANDLE_NONE;
        s_subscribed = false;
        s_evt_subscribed = false;
        hide_passkey();
        s_state = BLE_LINK_ADVERTISING;
        update_bridge_session();
        advertise();
        report();
        return 0;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        advertise();
        return 0;

    case BLE_GAP_EVENT_ENC_CHANGE:
        if (event->enc_change.status == 0) {
            on_secured(event->enc_change.conn_handle);
        } else {
            ESP_LOGW(TAG, "securing link failed, status %d", event->enc_change.status);
            hide_passkey();
            ble_gap_terminate(event->enc_change.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        }
        return 0;

    case BLE_GAP_EVENT_PASSKEY_ACTION: {
        if (event->passkey.params.action != BLE_SM_IOACT_DISP || !pairing_allowed()) {
            ESP_LOGW(TAG, "pairing refused");
            ble_gap_terminate(event->passkey.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
            return 0;
        }
        struct ble_sm_io io = {
            .action = BLE_SM_IOACT_DISP,
            .passkey = esp_random() % PASSKEY_MAX,
        };
        s_passkey_shown = true;
        if (s_cb.passkey) {
            s_cb.passkey(io.passkey, true, s_cb_ctx);
        }
        ble_sm_inject_io(event->passkey.conn_handle, &io);
        return 0;
    }

    case BLE_GAP_EVENT_REPEAT_PAIRING: {
        /* A bonded phone lost its keys and wants to pair again: only allowed through the window. */
        if (!s_window_open) {
            return BLE_GAP_REPEAT_PAIRING_IGNORE;
        }
        struct ble_gap_conn_desc desc;
        if (ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc) == 0) {
            ble_store_util_delete_peer(&desc.peer_id_addr);
        }
        return BLE_GAP_REPEAT_PAIRING_RETRY;
    }

    case BLE_GAP_EVENT_SUBSCRIBE:
        if (event->subscribe.attr_handle == s_tx_handle) {
            s_subscribed = event->subscribe.cur_notify;
        } else if (event->subscribe.attr_handle == s_evt_handle) {
            s_evt_subscribed = event->subscribe.cur_notify;
            update_bridge_session();
        }
        return 0;

    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "mtu %d", event->mtu.value);
        return 0;

    default:
        return 0;
    }
}

static void advertise(void)
{
    if (ble_gap_adv_active() || s_conn != BLE_HS_CONN_HANDLE_NONE) {
        return;
    }

    const char *name = ble_svc_gap_device_name();
    struct ble_hs_adv_fields fields = {
        .flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP,
        .name = (const uint8_t *)name,
        .name_len = strlen(name),
        .name_is_complete = 1,
    };
    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv fields: %d", rc);
        return;
    }

    struct ble_hs_adv_fields rsp = {
        .uuids128 = &NUS_SVC_UUID,
        .num_uuids128 = 1,
        .uuids128_is_complete = 1,
    };
    rc = ble_gap_adv_rsp_set_fields(&rsp);
    if (rc != 0) {
        ESP_LOGE(TAG, "scan response fields: %d", rc);
        return;
    }

    struct ble_gap_adv_params params = {
        .conn_mode = BLE_GAP_CONN_MODE_UND,
        .disc_mode = BLE_GAP_DISC_MODE_GEN,
        .itvl_min = ADV_ITVL_MIN,
        .itvl_max = ADV_ITVL_MAX,
    };
    rc = ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER, &params, gap_event_cb, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv start: %d", rc);
    }
}

static void on_sync(void)
{
    ble_hs_util_ensure_addr(0);
    ble_hs_id_infer_auto(0, &s_own_addr_type);
    advertise();
    report();
}

static void on_reset(int reason)
{
    ESP_LOGE(TAG, "host reset, reason %d", reason);
}

static void host_task(void *arg)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static void window_timer_cb(void *arg)
{
    ble_link_pairing_window(false);
}

esp_err_t ble_link_start(const char *device_name, const ble_link_callbacks_t *callbacks, void *ctx)
{
    s_cb = *callbacks;
    s_cb_ctx = ctx;

    const esp_timer_create_args_t timer_args = {
        .callback = window_timer_cb,
        .name = "ble_pair_window",
    };
    ESP_RETURN_ON_ERROR(esp_timer_create(&timer_args, &s_window_timer), TAG, "timer");
    ESP_RETURN_ON_ERROR(nimble_port_init(), TAG, "nimble init");

    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.store_status_cb = store_status_cb;
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_DISPLAY_ONLY;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 1;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

    ble_svc_gap_init();
    ble_svc_gatt_init();
    ESP_RETURN_ON_FALSE(ble_gatts_count_cfg(s_services) == 0, ESP_FAIL, TAG, "gatt count");
    ESP_RETURN_ON_FALSE(ble_gatts_add_svcs(s_services) == 0, ESP_FAIL, TAG, "gatt add");
    ESP_RETURN_ON_FALSE(ble_svc_gap_device_name_set(device_name) == 0, ESP_ERR_INVALID_ARG, TAG, "name");

    ble_store_config_init();
    nimble_port_freertos_init(host_task);
    return ESP_OK;
}

void ble_link_pairing_window(bool open)
{
    if (open == s_window_open) {
        return;
    }
    s_window_open = open;
    esp_timer_stop(s_window_timer);
    if (open) {
        esp_timer_start_once(s_window_timer, BLE_LINK_PAIRING_WINDOW_S * 1000000ULL);
    }
    ESP_LOGI(TAG, "pairing window %s", open ? "open" : "closed");
    report();
}

esp_err_t ble_link_forget(void)
{
    ble_link_disconnect();
    int rc = ble_store_clear();
    int count = -1;
    ble_store_util_count(BLE_STORE_OBJ_TYPE_PEER_SEC, &count);
    ESP_LOGI(TAG, "forget: clear rc=%d, bonds left=%d", rc, count);
    report();
    return rc == 0 ? ESP_OK : ESP_FAIL;
}

void ble_link_disconnect(void)
{
    if (s_conn != BLE_HS_CONN_HANDLE_NONE) {
        ble_gap_terminate(s_conn, BLE_ERR_REM_USER_CONN_TERM);
    }
}

esp_err_t ble_link_notify(const void *data, size_t len)
{
    if (s_state != BLE_LINK_CONNECTED || !s_subscribed) {
        return ESP_ERR_INVALID_STATE;
    }
    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, len);
    ESP_RETURN_ON_FALSE(om, ESP_ERR_NO_MEM, TAG, "mbuf");
    return ble_gatts_notify_custom(s_conn, s_tx_handle, om) == 0 ? ESP_OK : ESP_FAIL;
}

esp_err_t ble_link_bridge_send(const uint8_t *data, size_t len)
{
    while (len) {
        uint16_t conn = s_conn;
        if (!s_bridge_session || conn == BLE_HS_CONN_HANDLE_NONE) {
            return ESP_ERR_INVALID_STATE;
        }
        size_t chunk = ble_att_mtu(conn) - 3;
        if (chunk > BLE_ATT_ATTR_MAX_LEN) {
            chunk = BLE_ATT_ATTR_MAX_LEN;
        }
        if (chunk > len) {
            chunk = len;
        }

        int rc = BLE_HS_ENOMEM;
        for (int attempt = 0; attempt < NOTIFY_RETRIES && rc == BLE_HS_ENOMEM; attempt++) {
            struct os_mbuf *om = ble_hs_mbuf_from_flat(data, chunk);
            rc = om ? ble_gatts_notify_custom(conn, s_evt_handle, om) : BLE_HS_ENOMEM;
            if (rc == BLE_HS_ENOMEM) {
                vTaskDelay(pdMS_TO_TICKS(NOTIFY_RETRY_MS));
            }
        }
        if (rc != 0) {
            ESP_LOGW(TAG, "bridge notify failed: %d", rc);
            return ESP_FAIL;
        }
        data += chunk;
        len -= chunk;
    }
    return ESP_OK;
}
