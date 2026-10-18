#include <string.h>
#include "canokey_port.h"
#include "ctaphid_glue.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "nvs.h"
#include <applets.h>
#include <apdu.h>
#include <ctap.h>
#include <ctap-internal.h>
#include <device.h>
#include <fs.h>
#include <lfs.h>
#include <platform-config.h>
#include <rand.h>

static const char *TAG = "canokey";

#define PARTITION_LABEL     "fido"
#define LFS_BLOCK_SIZE      4096
#define LFS_CACHE_SIZE      256
#define LFS_LOOKAHEAD_SIZE  32
#define TASK_STACK          10240
#define TASK_PRIO           5
#define LOOP_IDLE_MS        2
#define LED_TICK_MS         20

#define NVS_NAMESPACE       "canokey"
#define NVS_CONFIG_PAGE     "cfgpage"

extern const uint8_t canokey_dev_attestation_key[32];
extern const uint8_t canokey_dev_attestation_cert[];
extern const size_t canokey_dev_attestation_cert_len;

static const esp_partition_t *s_part;
static uint8_t s_read_buf[LFS_CACHE_SIZE];
static uint8_t s_prog_buf[LFS_CACHE_SIZE];
static uint8_t s_lookahead_buf[LFS_LOOKAHEAD_SIZE];
static struct lfs_config s_lfs_cfg;

static canokey_presence_cb_t s_presence_cb;
static void *s_presence_ctx;
static bool s_presence_waiting;

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

/* ---- platform config page (canokey settings), kept in NVS ---------------- */

int platform_config_page_read(size_t off, void *buf, size_t len)
{
    if (!buf || off > PLATFORM_CONFIG_PAGE_SIZE || len > PLATFORM_CONFIG_PAGE_SIZE - off) {
        return -1;
    }
    uint8_t page[PLATFORM_CONFIG_PAGE_SIZE];
    memset(page, 0xFF, sizeof(page));
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) == ESP_OK) {
        size_t page_len = sizeof(page);
        if (nvs_get_blob(nvs, NVS_CONFIG_PAGE, page, &page_len) != ESP_OK || page_len != sizeof(page)) {
            memset(page, 0xFF, sizeof(page));
        }
        nvs_close(nvs);
    }
    memcpy(buf, page + off, len);
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
    return err == ESP_OK ? 0 : -1;
}

/* ---- littlefs on the "fido" partition ------------------------------------ */

static int lfs_read(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, void *buf, lfs_size_t size)
{
    return esp_partition_read(s_part, block * c->block_size + off, buf, size) == ESP_OK ? LFS_ERR_OK : LFS_ERR_IO;
}

static int lfs_prog(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, const void *buf, lfs_size_t size)
{
    return esp_partition_write(s_part, block * c->block_size + off, buf, size) == ESP_OK ? LFS_ERR_OK : LFS_ERR_IO;
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

static esp_err_t fs_start(void)
{
    s_part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, PARTITION_LABEL);
    ESP_RETURN_ON_FALSE(s_part, ESP_ERR_NOT_FOUND, TAG, "no '%s' partition", PARTITION_LABEL);

    s_lfs_cfg = (struct lfs_config){
        .read = lfs_read,
        .prog = lfs_prog,
        .erase = lfs_erase,
        .sync = lfs_sync,
        .read_size = 16,
        .prog_size = 16,
        .block_size = LFS_BLOCK_SIZE,
        .block_count = s_part->size / LFS_BLOCK_SIZE,
        .block_cycles = 500,
        .cache_size = LFS_CACHE_SIZE,
        .lookahead_size = LFS_LOOKAHEAD_SIZE,
        .read_buffer = s_read_buf,
        .prog_buffer = s_prog_buf,
        .lookahead_buffer = s_lookahead_buf,
    };
    if (fs_mount(&s_lfs_cfg) < 0) {
        ESP_LOGW(TAG, "formatting '%s'", PARTITION_LABEL);
        ESP_RETURN_ON_FALSE(fs_format(&s_lfs_cfg) == 0, ESP_FAIL, TAG, "format");
        ESP_RETURN_ON_FALSE(fs_mount(&s_lfs_cfg) == 0, ESP_FAIL, TAG, "mount");
    }
    return ESP_OK;
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

/* canokey asks for presence by "blinking"; the blink state is polled to drive the UI prompt. */
static void led_timer_cb(void *arg)
{
    device_update_led();
    bool waiting = device_is_blinking();
    if (waiting != s_presence_waiting) {
        s_presence_waiting = waiting;
        if (s_presence_cb) {
            s_presence_cb(waiting, s_presence_ctx);
        }
    }
}

void canokey_confirm_presence(void)
{
    if (s_presence_waiting) {
        set_touch_result(TOUCH_SHORT);
    }
}

/* ---- task ------------------------------------------------------------------ */

static void canokey_task(void *arg)
{
    for (;;) {
        device_loop();
        vTaskDelay(pdMS_TO_TICKS(LOOP_IDLE_MS));
    }
}

esp_err_t canokey_start(canokey_presence_cb_t presence_cb, void *ctx)
{
    s_presence_cb = presence_cb;
    s_presence_ctx = ctx;

    ESP_RETURN_ON_ERROR(fs_start(), TAG, "fs");
    device_init();
    ESP_RETURN_ON_FALSE(applets_install() == 0, ESP_FAIL, TAG, "applets install");
    ESP_RETURN_ON_ERROR(install_dev_attestation(), TAG, "attestation");
    ESP_RETURN_ON_ERROR(ctaphid_glue_start(), TAG, "ctaphid");

    const esp_timer_create_args_t timer_args = { .callback = led_timer_cb, .name = "canokey_led" };
    esp_timer_handle_t timer;
    ESP_RETURN_ON_ERROR(esp_timer_create(&timer_args, &timer), TAG, "timer");
    ESP_RETURN_ON_ERROR(esp_timer_start_periodic(timer, LED_TICK_MS * 1000), TAG, "timer start");

    BaseType_t ok = xTaskCreate(canokey_task, "canokey", TASK_STACK, NULL, TASK_PRIO, NULL);
    ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_ERR_NO_MEM, TAG, "task");
    ESP_LOGI(TAG, "started: FIDO filesystem %u KB", (unsigned)(s_part->size / 1024));
    return ESP_OK;
}
