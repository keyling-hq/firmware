#include <string.h>
#include "canokey_port.h"
#include "canokey_gate.h"
#include "canokey_storage.h"
#include "ctap_request.h"
#include "ccid_glue.h"
#include "ctaphid_glue.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "usb_hid.h"
#include "nvs.h"
#include <stdio.h>
#include <apdu.h>
#include <applets.h>
#include <ctap.h>
#include <openpgp.h>
#include <ctap-internal.h>
#include <ctaphid.h>
#include <ccid.h>
#include <device.h>
#include <device-config.h>
#include <fs.h>
#include <platform-config.h>
#include <rand.h>

static const char *TAG = "canokey";

#define TASK_STACK          12288
#define TASK_PRIO           2
#define LOOP_IDLE_MS        2

#define NVS_NAMESPACE       "canokey"
#define NVS_CONFIG_PAGE     "cfgpage"
#define NVS_DEFAULT_PROFILE "profile"

extern const uint8_t canokey_dev_attestation_key[32];
extern const uint8_t canokey_dev_attestation_cert[];
extern const size_t canokey_dev_attestation_cert_len;

static const char *s_status = "not started";
static TaskHandle_t s_task;
static volatile bool s_running;
static volatile uint32_t s_loop_tick;
#define STUCK_MS            5000
static int s_boot_fs_used;
static int s_boot_fs_size;
static int s_boot_cert_size;
static canokey_presence_cb_t s_presence_cb;
static canokey_event_cb_t s_event_cb;
static void *s_cb_ctx;
static volatile bool s_presence_waiting;
static volatile bool s_fido_wipe_pending;

static volatile canokey_profile_t s_profile;
static canokey_profile_t s_default_profile;
static volatile bool s_profile_open;
static canokey_profile_t s_store_profile;
static volatile canokey_profile_t s_reset_target;
static canokey_profile_t s_fido_wipe_target;

/* ---- platform hooks required by canokey-core (include/device.h) ---------- */

void device_delay(int ms)
{
    vTaskDelay(ms > 0 ? pdMS_TO_TICKS(ms) + 1 : 1);
}

uint32_t device_get_tick(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

int device_spinlock_lock(volatile uint32_t *lock, uint32_t blocking)
{
    uint32_t expected = 0;
    while (!__atomic_compare_exchange_n(lock, &expected, 1, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
        if (!blocking) {
            return -1;
        }
        expected = 0;
        taskYIELD();
    }
    return 0;
}

void device_spinlock_unlock(volatile uint32_t *lock)
{
    __atomic_store_n(lock, 0, __ATOMIC_RELEASE);
}

int device_atomic_compare_and_swap(volatile uint32_t *var, uint32_t expect, uint32_t update)
{
    return __atomic_compare_exchange_n(var, &expect, update, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST) ? 0 : -1;
}

/* The status LED belongs to the app: canokey's blink requests only drive the presence prompt. */
void led_on(void)
{
}

void led_off(void)
{
}

/*
 * One-shot timer for the CCID time extensions sent while an APDU takes long. The lock lets
 * device_set_timeout(NULL, 0) wait out a callback already running, which re-arms itself.
 */
static esp_timer_handle_t s_timeout_timer;
static SemaphoreHandle_t s_timeout_lock;
static void (*s_timeout_cb)(void);

static void timeout_timer_cb(void *arg)
{
    xSemaphoreTakeRecursive(s_timeout_lock, portMAX_DELAY);
    void (*callback)(void) = s_timeout_cb;
    s_timeout_cb = NULL;
    if (callback) {
        callback();
    }
    xSemaphoreGiveRecursive(s_timeout_lock);
}

void device_set_timeout(void (*callback)(void), uint16_t timeout)
{
    xSemaphoreTakeRecursive(s_timeout_lock, portMAX_DELAY);
    esp_timer_stop(s_timeout_timer);
    s_timeout_cb = callback;
    if (callback) {
        esp_timer_start_once(s_timeout_timer, timeout * 1000ULL);
    }
    xSemaphoreGiveRecursive(s_timeout_lock);
}

uint32_t random32(void)
{
    return esp_random();
}

void random_buffer(uint8_t *buf, size_t len)
{
    esp_fill_random(buf, len);
}

/*
 * Platform config page (canokey settings), stored in NVS and cached in RAM: canokey reads it
 * often (LED state on every blink update), sometimes from small stacks.
 */
static uint8_t s_config_page[PLATFORM_CONFIG_PAGE_SIZE];
static bool s_config_loaded;

static void config_page_load(void)
{
    memset(s_config_page, 0xFF, sizeof(s_config_page));
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) == ESP_OK) {
        size_t len = sizeof(s_config_page);
        if (nvs_get_blob(nvs, NVS_CONFIG_PAGE, s_config_page, &len) != ESP_OK || len != sizeof(s_config_page)) {
            memset(s_config_page, 0xFF, sizeof(s_config_page));
        }
        nvs_close(nvs);
    }
    s_config_loaded = true;
}

int platform_config_page_read(size_t off, void *buf, size_t len)
{
    if (!buf || off > PLATFORM_CONFIG_PAGE_SIZE || len > PLATFORM_CONFIG_PAGE_SIZE - off) {
        return -1;
    }
    if (!s_config_loaded) {
        config_page_load();
    }
    memcpy(buf, s_config_page + off, len);
    return 0;
}

int platform_config_page_write(const void *page, size_t len)
{
    if (!page || len != PLATFORM_CONFIG_PAGE_SIZE) {
        return -1;
    }
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs) != ESP_OK) {
        return -1;
    }
    esp_err_t err = nvs_set_blob(nvs, NVS_CONFIG_PAGE, page, len);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    if (err != ESP_OK) {
        return -1;
    }
    memcpy(s_config_page, page, sizeof(s_config_page));
    s_config_loaded = true;
    return 0;
}

/* ---- applets ------------------------------------------------------------- */

static char s_status_buf[32];

static esp_err_t install_failed(const char *applet, int rc)
{
    snprintf(s_status_buf, sizeof(s_status_buf), "%s install %d", applet, rc);
    s_status = s_status_buf;
    ESP_LOGE(TAG, "%s", s_status_buf);
    return ESP_FAIL;
}

/* Replaces canokey's applets_install(): only FIDO and OpenPGP are built. */
static esp_err_t install_applets(void)
{
    int rc = ctap_install(0);
    if (rc != 0) {
        return install_failed("ctap", rc);
    }
    rc = openpgp_install(0);
    if (rc < 0) {
        return install_failed("openpgp", rc);
    }
    return ESP_OK;
}

void applets_poweroff(void)
{
    ctap_poweroff();
    openpgp_poweroff();
}

/* Settings the admin applet would otherwise hold. */
uint8_t device_config_is_webauthn_enabled(void)
{
    return 1;
}

uint8_t device_config_is_led_normally_on(void)
{
    return 0;
}

uint8_t device_config_is_openpgp_ccid_enabled(void)
{
    return 1;
}

uint8_t device_config_is_openpgp_nfc_enabled(void)
{
    return 0;
}

uint8_t device_config_is_piv_ccid_enabled(void)
{
    return 0;
}

uint8_t device_config_is_piv_nfc_enabled(void)
{
    return 0;
}

/*
 * The OpenPGP card's serial number: the low bytes of the chip's MAC address, stable per device.
 * Each profile's card has its own, so the host sees a different card.
 */
void device_config_fill_serial(uint8_t *buf)
{
    uint8_t mac[6] = { 0 };
    esp_efuse_mac_get_default(mac);
    memcpy(buf, mac + 2, 4);
    if (s_profile == CANOKEY_PROFILE_PROTECTED) {
        buf[0] ^= 0x80;
    }
}

/* ---- attestation --------------------------------------------------------- */

/* Development key and certificate: public, so they attest nothing. */
static esp_err_t install_dev_attestation(void)
{
    if (get_file_size(CTAP_CERT_FILE) > 0) {
        return ESP_OK;
    }
    ESP_LOGW(TAG, "installing DEVELOPMENT attestation key and certificate");
    uint8_t rbuf[16];
    RAPDU rapdu = { .data = rbuf };
    CAPDU capdu = {
        .data = (uint8_t *)canokey_dev_attestation_key,
        .lc = sizeof(canokey_dev_attestation_key),
    };
    ESP_RETURN_ON_FALSE(ctap_install_private_key(&capdu, &rapdu) == 0, ESP_FAIL, TAG, "attestation key");
    capdu.data = (uint8_t *)canokey_dev_attestation_cert;
    capdu.lc = canokey_dev_attestation_cert_len;
    ESP_RETURN_ON_FALSE(ctap_install_cert(&capdu, &rapdu) == 0, ESP_FAIL, TAG, "attestation cert");
    return ESP_OK;
}

/* ---- user presence -------------------------------------------------------- */

#define PRESENCE_TIMEOUT_MS     15000
#define KEEPALIVE_INTERVAL_MS   100

static void report_presence(bool waiting, bool approved)
{
    canokey_request_t kind;
    const char *rp_id;
    ctap_request_current(&kind, &rp_id);
    if (s_presence_cb) {
        s_presence_cb(waiting, kind, rp_id, approved, s_cb_ctx);
    }
}

/*
 * Replaces canokey-core's wait_for_user_presence() (linked with -Wl,--wrap): the same loop as
 * src/device.c, with a shorter timeout and the prompt driven directly from here.
 */
uint8_t __wrap_wait_for_user_presence(uint8_t entry)
{
    device_applet_session_owner_t owner = device_applet_session_owner();
    if (owner == DEVICE_APPLET_SESSION_NONE) {
        owner = entry == WAIT_ENTRY_CTAPHID ? DEVICE_APPLET_SESSION_CTAPHID : DEVICE_APPLET_SESSION_CCID;
    }
    /* A new presence test is denied while one is ongoing. */
    if (s_presence_waiting) {
        return USER_PRESENCE_TIMEOUT;
    }
    s_presence_waiting = true;
    set_touch_result(TOUCH_NO);
    report_presence(true, false);

    uint8_t result = USER_PRESENCE_OK;
    uint32_t start = device_get_tick();
    uint32_t last = start;
    while (get_touch_result() == TOUCH_NO) {
        device_applet_session_touch(owner);
        start_blinking(0);
        if (CTAPHID_Loop(1) == LOOP_CANCEL && owner == DEVICE_APPLET_SESSION_CTAPHID) {
            result = USER_PRESENCE_CANCEL;
            break;
        }
        uint32_t now = device_get_tick();
        if (now - start >= PRESENCE_TIMEOUT_MS) {
            result = USER_PRESENCE_TIMEOUT;
            break;
        }
        if (now - last >= KEEPALIVE_INTERVAL_MS) {
            last = now;
            if (owner == DEVICE_APPLET_SESSION_CTAPHID) {
                CTAPHID_SendKeepAlive(KEEPALIVE_STATUS_UPNEEDED);
            }
        }
    }
    set_touch_result(TOUCH_NO);
    stop_blinking();
    s_presence_waiting = false;
    report_presence(false, result == USER_PRESENCE_OK);
    return result;
}

void canokey_confirm_presence(void)
{
    if (s_presence_waiting) {
        set_touch_result(TOUCH_SHORT);
    }
}

void canokey_confirm_reset(canokey_profile_t profile)
{
    if (s_presence_waiting && profile < CANOKEY_PROFILE_COUNT) {
        s_reset_target = profile;
        set_touch_result(TOUCH_SHORT);
    }
}

/* ---- applet session -------------------------------------------------------- */

/* canokey lets another transport take over a session idle for 2 s, so this always ends with it. */
#define SESSION_WAIT_MS     2500

int __real_device_applet_session_acquire(device_applet_session_owner_t owner);

/*
 * Replaces canokey-core's device_applet_session_acquire() for its callers (linked with -Wl,--wrap).
 * A FIDO request arriving while a smartcard command is in progress waited for nothing upstream
 * and was answered busy. Hosts probe the card right after plug-in, exactly when Windows' security
 * key reset needs the FIDO interface, so the smartcard command is run to completion first.
 */
int __wrap_device_applet_session_acquire(device_applet_session_owner_t owner)
{
    int rc = __real_device_applet_session_acquire(owner);
    if (rc == 0 || owner != DEVICE_APPLET_SESSION_CTAPHID || device_applet_session_owner() != DEVICE_APPLET_SESSION_CCID) {
        return rc;
    }
    uint32_t start = device_get_tick();
    while (rc != 0 && device_get_tick() - start < SESSION_WAIT_MS) {
        ccid_glue_poll();
        /* A response the host hasn't collected yet would make every new one wait for it. */
        if (!ccid_glue_sending()) {
            CCID_Loop();
        }
        device_delay(1);
        rc = __real_device_applet_session_acquire(owner);
    }
    ESP_LOGW(TAG, "FIDO waited %lu ms for a smartcard command: %s", (unsigned long)(device_get_tick() - start),
             rc == 0 ? "served" : "answered busy");
    return rc;
}

/* ---- profiles ------------------------------------------------------------ */

static void notify(canokey_event_t event)
{
    if (s_event_cb) {
        s_event_cb(event, s_cb_ctx);
    }
}

bool canokey_port_locked(void)
{
    return !s_profile_open;
}

/*
 * The card is in the slot while the active profile is open. A switch takes the card out and puts
 * the new one in after a pause, so the host notices the change; the host is told each time over
 * the interrupt endpoint, and the slot reports what it was last told.
 */
#define CARD_SWAP_MS        300

static volatile bool s_card_swap;
static bool s_card_present;
static uint32_t s_card_removed_tick;

uint8_t canokey_port_card_absent(void)
{
    return !s_card_present;
}

static void update_card(void)
{
    if (!usb_hid_is_mounted()) {
        s_card_present = false;
        return;
    }
    uint32_t now = device_get_tick();
    if (s_card_swap && !s_card_present && now - s_card_removed_tick >= CARD_SWAP_MS) {
        s_card_swap = false;
    }
    bool present = s_profile_open && !s_card_swap;
    if (present == s_card_present || ccid_glue_notify_slot(present) != ESP_OK) {
        return;
    }
    s_card_present = present;
    s_card_removed_tick = now;
}

/* Hosts retry and probe in bursts: one report per burst is enough. */
#define LOCKED_REPORT_MS    3000

void canokey_port_report_locked(void)
{
    static uint32_t last;
    static bool reported;
    uint32_t now = device_get_tick();
    if (reported && now - last < LOCKED_REPORT_MS) {
        return;
    }
    reported = true;
    last = now;
    ESP_LOGW(TAG, "request refused: profile %d is locked", s_profile);
    notify(CANOKEY_EVENT_LOCKED_REQUEST);
}

/*
 * Runs the applets on a profile's stores. A locked profile's can't be read: the applets run on the
 * default profile's instead, so getInfo and resets still work, and every other request is refused.
 */
static esp_err_t activate(canokey_profile_t profile)
{
    applets_poweroff();
    s_profile = profile;
    s_profile_open = false;
    s_reset_target = profile;
    bool open = canokey_storage_state(profile) == CANOKEY_PROFILE_OPEN;
    s_store_profile = open ? profile : CANOKEY_PROFILE_DEFAULT;
    ESP_RETURN_ON_ERROR(canokey_storage_select(s_store_profile), TAG, "select");
    ESP_RETURN_ON_ERROR(install_applets(), TAG, "applets install");
    ESP_RETURN_ON_ERROR(install_dev_attestation(), TAG, "attestation");
    s_profile_open = open;
    ESP_LOGI(TAG, "profile %d: %s", profile, open ? "active" : "locked");
    return ESP_OK;
}

static canokey_profile_t default_profile_load(void)
{
    uint8_t profile = CANOKEY_PROFILE_DEFAULT;
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) == ESP_OK) {
        nvs_get_u8(nvs, NVS_DEFAULT_PROFILE, &profile);
        nvs_close(nvs);
    }
    return profile < CANOKEY_PROFILE_COUNT ? profile : CANOKEY_PROFILE_DEFAULT;
}

static esp_err_t default_profile_store(canokey_profile_t profile)
{
    nvs_handle_t nvs;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs), TAG, "nvs");
    esp_err_t err = nvs_set_u8(nvs, NVS_DEFAULT_PROFILE, profile);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    if (err == ESP_OK) {
        s_default_profile = profile;
    }
    return err;
}

void canokey_profiles_get(canokey_profiles_t *out)
{
    *out = (canokey_profiles_t){
        .active = s_profile,
        .default_profile = s_default_profile,
        .protected_state = canokey_storage_state(CANOKEY_PROFILE_PROTECTED),
    };
    out->has_protected_id = canokey_storage_protected_id(out->protected_id);
}

/* ---- commands from other tasks, run on the canokey task ---------------------- */

typedef enum {
    CMD_SELECT,
    CMD_SECRET,
    CMD_RESET,
} cmd_kind_t;

typedef struct {
    cmd_kind_t kind;
    canokey_profile_t profile;
    bool make_default;
    uint8_t secret[CANOKEY_SECRET_SIZE];
    size_t secret_len;
    esp_err_t *result;      /* with done: the caller waits */
    SemaphoreHandle_t done;
} cmd_t;

#define CMD_QUEUE_LEN       4

static QueueHandle_t s_cmds;
static SemaphoreHandle_t s_cmd_lock;
static SemaphoreHandle_t s_cmd_done;

static esp_err_t run_cmd(cmd_t *cmd)
{
    esp_err_t err = ESP_OK;
    switch (cmd->kind) {
    case CMD_SELECT:
        if (cmd->make_default) {
            err = default_profile_store(cmd->profile);
        }
        if (err == ESP_OK && cmd->profile != s_profile) {
            s_card_swap = true;
            err = activate(cmd->profile);
        }
        break;
    case CMD_SECRET:
        err = canokey_storage_set_secret(cmd->secret, cmd->secret_len);
        if (err == ESP_OK && s_profile == CANOKEY_PROFILE_PROTECTED && !s_profile_open) {
            err = activate(CANOKEY_PROFILE_PROTECTED);
        }
        break;
    case CMD_RESET:
        if (cmd->profile == s_store_profile) {
            applets_poweroff();
            s_profile_open = false;
        }
        err = canokey_storage_wipe_profile(cmd->profile);
        if (err == ESP_OK && cmd->profile == s_store_profile) {
            err = activate(s_profile);
        }
        ESP_LOGI(TAG, "profile %d reset: %s", cmd->profile, esp_err_to_name(err));
        break;
    }
    memset(cmd->secret, 0, sizeof(cmd->secret));
    notify(CANOKEY_EVENT_PROFILES);
    return err;
}

static void poll_cmds(void)
{
    cmd_t cmd;
    while (xQueueReceive(s_cmds, &cmd, 0) == pdTRUE) {
        esp_err_t err = run_cmd(&cmd);
        if (cmd.done) {
            *cmd.result = err;
            xSemaphoreGive(cmd.done);
        }
    }
}

static esp_err_t post(cmd_t *cmd, bool wait)
{
    ESP_RETURN_ON_FALSE(s_running, ESP_ERR_INVALID_STATE, TAG, "not running");
    ESP_RETURN_ON_FALSE(xTaskGetCurrentTaskHandle() != s_task, ESP_ERR_INVALID_STATE, TAG, "own task");
    if (!wait) {
        esp_err_t err = xQueueSend(s_cmds, cmd, 0) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
        memset(cmd->secret, 0, sizeof(cmd->secret));
        return err;
    }
    esp_err_t result = ESP_FAIL;
    xSemaphoreTake(s_cmd_lock, portMAX_DELAY);
    cmd->result = &result;
    cmd->done = s_cmd_done;
    esp_err_t err = xQueueSend(s_cmds, cmd, portMAX_DELAY) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
    memset(cmd->secret, 0, sizeof(cmd->secret));
    if (err == ESP_OK) {
        xSemaphoreTake(s_cmd_done, portMAX_DELAY);
        err = result;
    }
    xSemaphoreGive(s_cmd_lock);
    return err;
}

esp_err_t canokey_select_profile(canokey_profile_t profile, bool make_default, bool wait)
{
    ESP_RETURN_ON_FALSE(profile < CANOKEY_PROFILE_COUNT, ESP_ERR_INVALID_ARG, TAG, "profile");
    cmd_t cmd = { .kind = CMD_SELECT, .profile = profile, .make_default = make_default };
    return post(&cmd, wait);
}

esp_err_t canokey_set_secret(const uint8_t *secret, size_t len)
{
    ESP_RETURN_ON_FALSE(len == CANOKEY_SECRET_SIZE, ESP_ERR_INVALID_SIZE, TAG, "secret size");
    cmd_t cmd = { .kind = CMD_SECRET, .secret_len = len };
    memcpy(cmd.secret, secret, len);
    return post(&cmd, true);
}

esp_err_t canokey_reset_profile(canokey_profile_t profile, bool wait)
{
    ESP_RETURN_ON_FALSE(profile < CANOKEY_PROFILE_COUNT, ESP_ERR_INVALID_ARG, TAG, "profile");
    cmd_t cmd = { .kind = CMD_RESET, .profile = profile };
    return post(&cmd, wait);
}

/* ---- security key reset ------------------------------------------------------ */

/*
 * Called by the generated ctap.c for an approved authenticatorReset, in place of ctap_install(1).
 * The prompt chose whose credentials are erased; only the active profile's applet is reset here.
 */
uint8_t canokey_port_fido_reset(void)
{
    s_fido_wipe_target = s_reset_target;
    uint8_t rc = s_fido_wipe_target == s_store_profile ? ctap_install(1) : 0;
    s_fido_wipe_pending = rc == 0;
    return rc;
}

/* Runs once the reset's response is out: erasing reuses the buffers it is sent from. */
static void wipe_fido(void)
{
    canokey_profile_t target = s_fido_wipe_target;
    bool active = target == s_store_profile;
    if (canokey_storage_wipe_fido(target) != ESP_OK ||
        (active && (ctap_install(1) != 0 || install_dev_attestation() != ESP_OK))) {
        s_status = "reset failed";
        ESP_LOGE(TAG, "security key reset: storage wipe failed");
    } else {
        ESP_LOGI(TAG, "security key reset: profile %d erased", target);
    }
    notify(CANOKEY_EVENT_PROFILES);
}

/* ---- task ------------------------------------------------------------------ */

/* Runs on the canokey task: its install paths need several KB of stack. */
static esp_err_t startup(void)
{
    s_status = "fs fail";
    ESP_RETURN_ON_ERROR(canokey_storage_start(), TAG, "storage");
    device_init();
    /* Points the APDU buffer shared by the applets at the CCID buffer. */
    init_apdu_buffer();
    s_default_profile = default_profile_load();
    s_status = "install fail";
    ESP_RETURN_ON_ERROR(activate(s_default_profile), TAG, "profile");
    s_boot_fs_used = get_fs_usage();
    s_boot_fs_size = get_fs_size();
    s_boot_cert_size = get_file_size(CTAP_CERT_FILE);
    s_status = "usb fail";
    ESP_RETURN_ON_ERROR(ctaphid_glue_start(), TAG, "ctaphid");
    ESP_RETURN_ON_ERROR(ccid_glue_start(), TAG, "ccid");

    s_status = "ok";
    ESP_LOGI(TAG, "started");
    return ESP_OK;
}

/*
 * Counts startups that never finished. Survives the reset a crash causes, so a startup that
 * keeps crashing is skipped instead of boot-looping the whole device.
 */
#define STARTUP_MAGIC           0x464944u
#define MAX_CRASHED_STARTUPS    2

static RTC_NOINIT_ATTR uint32_t s_startup_magic;
static RTC_NOINIT_ATTR uint32_t s_crashed_startups;

static bool startup_crashed_before(void)
{
    if (s_startup_magic != STARTUP_MAGIC || esp_reset_reason() == ESP_RST_POWERON) {
        s_startup_magic = STARTUP_MAGIC;
        s_crashed_startups = 0;
    }
    return s_crashed_startups >= MAX_CRASHED_STARTUPS;
}

static void canokey_task(void *arg)
{
    if (startup_crashed_before()) {
        s_status = "skipped after crash";
        ESP_LOGE(TAG, "startup crashed %lu times: skipped until power-on", (unsigned long)s_crashed_startups);
        vTaskDelete(NULL);
    }
    s_crashed_startups++;
    esp_err_t err = startup();
    s_crashed_startups = 0;
    if (err != ESP_OK) {
        vTaskDelete(NULL);
    }
    s_running = true;
    notify(CANOKEY_EVENT_PROFILES);
    for (;;) {
        s_loop_tick = device_get_tick();
        poll_cmds();
        update_card();
        ccid_glue_poll();
        device_loop();
        if (s_fido_wipe_pending && !CTAPHID_TxBusy()) {
            s_fido_wipe_pending = false;
            wipe_fido();
        }
        device_update_led();
        vTaskDelay(pdMS_TO_TICKS(LOOP_IDLE_MS));
    }
}

const char *canokey_status(void)
{
    return s_status;
}

void canokey_log_diagnostics(void)
{
    ESP_LOGI(TAG, "status: %s", s_status);
    if (!s_running) {
        return;
    }
    /* Captured at startup: canokey's filesystem must only be touched from its own task. */
    ESP_LOGI(TAG, "at boot: fs %d of %d blocks used, attestation cert %d bytes", s_boot_fs_used, s_boot_fs_size,
             s_boot_cert_size);
    ESP_LOGI(TAG, "presence: waiting=%d blinking=%d touch=%d", s_presence_waiting, device_is_blinking(),
             get_touch_result());
    uint32_t since_loop = device_get_tick() - s_loop_tick;
    ESP_LOGI(TAG, "task: stack free %u bytes, tick %lu, loop ran %lu ms ago",
             (unsigned)uxTaskGetStackHighWaterMark(s_task), (unsigned long)device_get_tick(),
             (unsigned long)since_loop);
    ctaphid_glue_log_diagnostics();
    ccid_glue_log_diagnostics();
    /* A stuck task is only found in a core dump: crashing now saves every task's stack. */
    if (since_loop > STUCK_MS && !s_presence_waiting) {
        ESP_LOGE(TAG, "task stuck for %lu ms: aborting for a core dump", (unsigned long)since_loop);
        vTaskDelay(pdMS_TO_TICKS(100));
        abort();
    }
}

esp_err_t canokey_start(canokey_presence_cb_t presence_cb, canokey_event_cb_t event_cb, void *ctx)
{
    s_presence_cb = presence_cb;
    s_event_cb = event_cb;
    s_cb_ctx = ctx;
    s_status = "starting";
    s_cmds = xQueueCreate(CMD_QUEUE_LEN, sizeof(cmd_t));
    s_cmd_lock = xSemaphoreCreateMutex();
    s_cmd_done = xSemaphoreCreateBinary();
    ESP_RETURN_ON_FALSE(s_cmds && s_cmd_lock && s_cmd_done, ESP_ERR_NO_MEM, TAG, "commands");
    s_timeout_lock = xSemaphoreCreateRecursiveMutex();
    ESP_RETURN_ON_FALSE(s_timeout_lock, ESP_ERR_NO_MEM, TAG, "timeout lock");
    const esp_timer_create_args_t timer_args = {
        .callback = timeout_timer_cb,
        .name = "canokey_timeout",
    };
    ESP_RETURN_ON_ERROR(esp_timer_create(&timer_args, &s_timeout_timer), TAG, "timeout timer");
    BaseType_t ok = xTaskCreate(canokey_task, "canokey", TASK_STACK, NULL, TASK_PRIO, &s_task);
    return ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}