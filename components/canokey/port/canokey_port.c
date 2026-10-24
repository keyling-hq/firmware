#include <string.h>
#include "canokey_port.h"
#include "ctap_request.h"
#include "ctaphid_glue.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_partition.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "keystore.h"
#include "nvs.h"
#include "aes/esp_aes.h"
#include <stdio.h>
#include <apdu.h>
#include <applets.h>
#include <ctap.h>
#include <ctap-internal.h>
#include <ctaphid.h>
#include <device.h>
#include <device-config.h>
#include <fs.h>
#include <lfs.h>
#include <platform-config.h>
#include <rand.h>

static const char *TAG = "canokey";

#define PARTITION_LABEL     "fido"
#define FS_BLOCK_SIZE       4096
#define FS_CACHE_SIZE       256
#define FS_LOOKAHEAD_SIZE   32
#define TASK_STACK          12288
#define TASK_PRIO           2
#define LOOP_IDLE_MS        2

#define NVS_NAMESPACE       "canokey"
#define NVS_CONFIG_PAGE     "cfgpage"
#define NVS_STORAGE_SALT    "fssalt"

#define STORAGE_SALT_SIZE   16
#define XTS_UNIT            16

extern const uint8_t canokey_dev_attestation_key[32];
extern const uint8_t canokey_dev_attestation_cert[];
extern const size_t canokey_dev_attestation_cert_len;

static const esp_partition_t *s_part;
static bool s_fs_mounted;
static uint8_t s_read_buf[FS_CACHE_SIZE];
static uint8_t s_prog_buf[FS_CACHE_SIZE];
static uint8_t s_lookahead_buf[FS_LOOKAHEAD_SIZE];
static uint8_t s_cipher_buf[FS_CACHE_SIZE];
static struct lfs_config s_lfs_cfg;
static esp_aes_xts_context s_xts_enc;
static esp_aes_xts_context s_xts_dec;
static volatile bool s_erase_pending;

static const char *s_status = "not started";
static TaskHandle_t s_task;
static volatile bool s_running;
static int s_boot_fs_used;
static int s_boot_fs_size;
static int s_boot_cert_size;
static canokey_presence_cb_t s_presence_cb;
static void *s_presence_ctx;
static volatile bool s_presence_waiting;

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

void device_set_timeout(void (*callback)(void), uint16_t timeout)
{
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

/* ---- littlefs on the "fido" partition ------------------------------------ */

/*
 * Everything on the partition is encrypted with XTS-AES-128 in 16-byte units, tweaked by the
 * unit's offset: the scheme of the chip's flash encryption, keyed from the keystore. A new salt
 * (after a security key reset) or a new keystore root makes all earlier contents unreadable.
 */
static esp_err_t storage_key_load(bool renew)
{
    uint8_t salt[STORAGE_SALT_SIZE];
    nvs_handle_t nvs;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs), TAG, "nvs");
    size_t len = sizeof(salt);
    esp_err_t err = ESP_OK;
    if (renew || nvs_get_blob(nvs, NVS_STORAGE_SALT, salt, &len) != ESP_OK || len != sizeof(salt)) {
        esp_fill_random(salt, sizeof(salt));
        err = nvs_set_blob(nvs, NVS_STORAGE_SALT, salt, sizeof(salt));
        if (err == ESP_OK) {
            err = nvs_commit(nvs);
        }
    }
    nvs_close(nvs);
    ESP_RETURN_ON_ERROR(err, TAG, "salt");

    uint8_t key[KEYSTORE_KEY_SIZE];
    err = keystore_derive("fido", salt, sizeof(salt), key);
    if (err == ESP_OK) {
        esp_aes_xts_free(&s_xts_enc);
        esp_aes_xts_free(&s_xts_dec);
        esp_aes_xts_init(&s_xts_enc);
        esp_aes_xts_init(&s_xts_dec);
        if (esp_aes_xts_setkey_enc(&s_xts_enc, key, 8 * sizeof(key)) != 0 ||
            esp_aes_xts_setkey_dec(&s_xts_dec, key, 8 * sizeof(key)) != 0) {
            err = ESP_FAIL;
        }
    }
    memset(key, 0, sizeof(key));
    return err;
}

static int crypt_units(esp_aes_xts_context *ctx, int mode, uint32_t addr, const uint8_t *in, uint8_t *out,
                       size_t size)
{
    for (size_t i = 0; i < size; i += XTS_UNIT) {
        uint8_t tweak[16] = { 0 };
        uint32_t unit_addr = addr + i;
        memcpy(tweak, &unit_addr, sizeof(unit_addr));
        if (esp_aes_crypt_xts(ctx, mode, XTS_UNIT, tweak, in + i, out + i) != 0) {
            return LFS_ERR_IO;
        }
    }
    return LFS_ERR_OK;
}

static int lfs_read(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, void *buf, lfs_size_t size)
{
    uint32_t addr = block * c->block_size + off;
    if (esp_partition_read(s_part, addr, buf, size) != ESP_OK) {
        return LFS_ERR_IO;
    }
    return crypt_units(&s_xts_dec, ESP_AES_DECRYPT, addr, buf, buf, size);
}

static int lfs_prog(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, const void *buf, lfs_size_t size)
{
    uint32_t addr = block * c->block_size + off;
    const uint8_t *in = buf;
    while (size) {
        size_t chunk = size < sizeof(s_cipher_buf) ? size : sizeof(s_cipher_buf);
        if (crypt_units(&s_xts_enc, ESP_AES_ENCRYPT, addr, in, s_cipher_buf, chunk) != LFS_ERR_OK ||
            esp_partition_write(s_part, addr, s_cipher_buf, chunk) != ESP_OK) {
            return LFS_ERR_IO;
        }
        addr += chunk;
        in += chunk;
        size -= chunk;
    }
    return LFS_ERR_OK;
}

static int lfs_erase(const struct lfs_config *c, lfs_block_t block)
{
    return esp_partition_erase_range(s_part, block * c->block_size, c->block_size) == ESP_OK ? LFS_ERR_OK
                                                                                               : LFS_ERR_IO;
}

static int lfs_sync(const struct lfs_config *c)
{
    return LFS_ERR_OK;
}

/* littlefs's format rewrites only its superblocks: the rest is erased so no old block survives. */
static int format_storage(void)
{
    if (esp_partition_erase_range(s_part, 0, s_part->size) != ESP_OK) {
        return LFS_ERR_IO;
    }
    return fs_format(&s_lfs_cfg);
}

static esp_err_t fs_start(void)
{
    if (s_fs_mounted) {
        return ESP_OK;
    }
    s_part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, PARTITION_LABEL);
    ESP_RETURN_ON_FALSE(s_part, ESP_ERR_NOT_FOUND, TAG, "no '%s' partition", PARTITION_LABEL);
    /* Without the key a failed mount must not lead to a format. */
    ESP_RETURN_ON_ERROR(storage_key_load(false), TAG, "storage key");

    s_lfs_cfg = (struct lfs_config){
        .read = lfs_read,
        .prog = lfs_prog,
        .erase = lfs_erase,
        .sync = lfs_sync,
        .read_size = 16,
        .prog_size = 16,
        .block_size = FS_BLOCK_SIZE,
        .block_count = s_part->size / FS_BLOCK_SIZE,
        .block_cycles = 500,
        .cache_size = FS_CACHE_SIZE,
        .lookahead_size = FS_LOOKAHEAD_SIZE,
        .read_buffer = s_read_buf,
        .prog_buffer = s_prog_buf,
        .lookahead_buffer = s_lookahead_buf,
    };
    if (fs_mount(&s_lfs_cfg) < 0) {
        /* Also the path after a factory reset: the new keystore salt changes the storage key. */
        ESP_LOGW(TAG, "formatting '%s'", PARTITION_LABEL);
        ESP_RETURN_ON_FALSE(format_storage() == 0, ESP_FAIL, TAG, "format");
        ESP_RETURN_ON_FALSE(fs_mount(&s_lfs_cfg) == 0, ESP_FAIL, TAG, "mount");
    }
    s_fs_mounted = true;
    return ESP_OK;
}

/* ---- applets ------------------------------------------------------------- */

static char s_status_buf[32];

/* Replaces canokey's applets_install(): only the FIDO applet is built. */
static esp_err_t install_applets(void)
{
    int rc = ctap_install(0);
    if (rc != 0) {
        snprintf(s_status_buf, sizeof(s_status_buf), "ctap install %d", rc);
        s_status = s_status_buf;
        ESP_LOGE(TAG, "%s", s_status_buf);
        return ESP_FAIL;
    }
    return ESP_OK;
}

void applets_poweroff(void)
{
    ctap_poweroff();
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
        s_presence_cb(waiting, kind, rp_id, approved, s_presence_ctx);
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
    if (result == USER_PRESENCE_OK) {
        canokey_request_t kind;
        const char *rp_id;
        ctap_request_current(&kind, &rp_id);
        s_erase_pending = kind == CANOKEY_REQUEST_RESET;
    }
    return result;
}

/*
 * canokey's reset deletes its files, but their old blocks would stay readable with the same key.
 * After an approved reset the storage gets a new key and a fresh filesystem instead.
 */
static void erase_storage(void)
{
    ESP_LOGW(TAG, "security key reset: erasing its storage");
    if (storage_key_load(true) != ESP_OK || format_storage() != 0 || fs_mount(&s_lfs_cfg) != 0 ||
        ctap_install(1) != 0 || install_dev_attestation() != ESP_OK) {
        s_status = "reset failed";
        ESP_LOGE(TAG, "%s", s_status);
    }
}

void canokey_port_request_done(void)
{
    if (s_erase_pending) {
        s_erase_pending = false;
        erase_storage();
    }
}

void canokey_confirm_presence(void)
{
    if (s_presence_waiting) {
        set_touch_result(TOUCH_SHORT);
    }
}
/* ---- task ------------------------------------------------------------------ */

/* Runs on the canokey task: its install paths need several KB of stack. */
static esp_err_t startup(void)
{
    s_status = "fs fail";
    ESP_RETURN_ON_ERROR(fs_start(), TAG, "fs");
    device_init();
    /* Without CCID nothing else points shared_io_buffer at memory: this selects the core's fallback. */
    init_apdu_buffer();
    ESP_RETURN_ON_ERROR(install_applets(), TAG, "applets install");
    s_status = "attest fail";
    ESP_RETURN_ON_ERROR(install_dev_attestation(), TAG, "attestation");
    s_boot_fs_used = get_fs_usage();
    s_boot_fs_size = get_fs_size();
    s_boot_cert_size = get_file_size(CTAP_CERT_FILE);
    s_status = "usb fail";
    ESP_RETURN_ON_ERROR(ctaphid_glue_start(), TAG, "ctaphid");

    s_status = "ok";
    ESP_LOGI(TAG, "started: FIDO filesystem %u KB", (unsigned)(s_part->size / 1024));
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
    for (;;) {
        device_loop();
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
    ESP_LOGI(TAG, "task: stack free %u bytes, tick %lu", (unsigned)uxTaskGetStackHighWaterMark(s_task),
             (unsigned long)device_get_tick());
    ctaphid_glue_log_diagnostics();
}

esp_err_t canokey_start(canokey_presence_cb_t presence_cb, void *ctx)
{
    s_presence_cb = presence_cb;
    s_presence_ctx = ctx;
    s_status = "starting";
    BaseType_t ok = xTaskCreate(canokey_task, "canokey", TASK_STACK, NULL, TASK_PRIO, &s_task);
    return ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}