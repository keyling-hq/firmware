#include <inttypes.h>
#include <string.h>
#include "ota_update.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "psa/crypto.h"

static const char *TAG = "ota";

static bool s_active;
static esp_ota_handle_t s_handle;
static const esp_partition_t *s_partition;
static psa_hash_operation_t s_hash;
static uint32_t s_size;
static uint32_t s_written;
static uint8_t s_expected[OTA_UPDATE_SHA256_LEN];

esp_err_t ota_update_begin(uint32_t size, const uint8_t *sha256, size_t sha256_len)
{
    ota_update_abort();
    ESP_RETURN_ON_FALSE(sha256_len == OTA_UPDATE_SHA256_LEN, ESP_ERR_INVALID_ARG, TAG, "sha256 length");

    s_partition = esp_ota_get_next_update_partition(NULL);
    ESP_RETURN_ON_FALSE(s_partition, ESP_ERR_NOT_FOUND, TAG, "no update partition");
    ESP_RETURN_ON_FALSE(size > 0 && size <= s_partition->size, ESP_ERR_INVALID_SIZE, TAG,
                        "image of %" PRIu32 " bytes doesn't fit %s", size, s_partition->label);

    ESP_RETURN_ON_FALSE(psa_crypto_init() == PSA_SUCCESS, ESP_FAIL, TAG, "psa init");
    s_hash = psa_hash_operation_init();
    ESP_RETURN_ON_FALSE(psa_hash_setup(&s_hash, PSA_ALG_SHA_256) == PSA_SUCCESS, ESP_FAIL, TAG, "hash");

    ESP_LOGI(TAG, "erasing %s for %" PRIu32 " bytes", s_partition->label, size);
    esp_err_t err = esp_ota_begin(s_partition, size, &s_handle);
    if (err != ESP_OK) {
        psa_hash_abort(&s_hash);
        ESP_LOGE(TAG, "begin: %s", esp_err_to_name(err));
        return err;
    }
    memcpy(s_expected, sha256, OTA_UPDATE_SHA256_LEN);
    s_size = size;
    s_written = 0;
    s_active = true;
    return ESP_OK;
}

esp_err_t ota_update_write(uint32_t offset, const uint8_t *data, size_t len)
{
    ESP_RETURN_ON_FALSE(s_active, ESP_ERR_INVALID_STATE, TAG, "no update running");
    ESP_RETURN_ON_FALSE(offset == s_written, ESP_ERR_INVALID_ARG, TAG,
                        "offset %" PRIu32 ", expected %" PRIu32, offset, s_written);
    ESP_RETURN_ON_FALSE(s_written + len <= s_size, ESP_ERR_INVALID_SIZE, TAG, "past the announced size");

    ESP_RETURN_ON_ERROR(esp_ota_write(s_handle, data, len), TAG, "write");
    ESP_RETURN_ON_FALSE(psa_hash_update(&s_hash, data, len) == PSA_SUCCESS, ESP_FAIL, TAG, "hash");
    s_written += len;
    return ESP_OK;
}

esp_err_t ota_update_end(void)
{
    ESP_RETURN_ON_FALSE(s_active, ESP_ERR_INVALID_STATE, TAG, "no update running");
    s_active = false;

    uint8_t digest[OTA_UPDATE_SHA256_LEN];
    size_t digest_len = 0;
    bool hashed = psa_hash_finish(&s_hash, digest, sizeof(digest), &digest_len) == PSA_SUCCESS;
    if (s_written != s_size || !hashed || memcmp(digest, s_expected, sizeof(digest)) != 0) {
        ESP_LOGE(TAG, "image rejected: %" PRIu32 "/%" PRIu32 " bytes, hash %s", s_written, s_size,
                 hashed ? "mismatch" : "failed");
        esp_ota_abort(s_handle);
        return ESP_ERR_INVALID_CRC;
    }

    ESP_RETURN_ON_ERROR(esp_ota_end(s_handle), TAG, "image validation");
    ESP_RETURN_ON_ERROR(esp_ota_set_boot_partition(s_partition), TAG, "set boot partition");
    ESP_LOGI(TAG, "update written to %s", s_partition->label);
    return ESP_OK;
}

void ota_update_abort(void)
{
    if (!s_active) {
        return;
    }
    s_active = false;
    esp_ota_abort(s_handle);
    psa_hash_abort(&s_hash);
    ESP_LOGW(TAG, "update abandoned at %" PRIu32 "/%" PRIu32 " bytes", s_written, s_size);
}

bool ota_update_active(void)
{
    return s_active;
}

uint8_t ota_update_progress(void)
{
    return s_size ? (uint8_t)((uint64_t)s_written * 100 / s_size) : 0;
}

void ota_update_confirm_boot(void)
{
    esp_ota_img_states_t state;
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (esp_ota_get_state_partition(running, &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY) {
        esp_ota_mark_app_valid_cancel_rollback();
        ESP_LOGI(TAG, "new firmware in %s confirmed", running->label);
    }
}
