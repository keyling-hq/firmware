#include <string.h>
#include "keystore.h"
#include "esp_check.h"
#include "esp_efuse.h"
#include "esp_flash_encrypt.h"
#include "esp_hmac.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_secure_boot.h"
#include "nvs.h"
#include "psa/crypto.h"

static const char *TAG = "keystore";

#define NVS_NAMESPACE   "keystore"
#define NVS_ROOT        "root"
#define NVS_SALT        "salt"

static keystore_status_t s_status;
static hmac_key_id_t s_efuse_key;
static psa_key_id_t s_dev_key;
static uint8_t s_salt[KEYSTORE_KEY_SIZE];
static bool s_ready;

/* Loads a random secret from NVS, creating it on first use. */
static esp_err_t load_or_create(nvs_handle_t nvs, const char *name, uint8_t out[KEYSTORE_KEY_SIZE])
{
    size_t len = KEYSTORE_KEY_SIZE;
    esp_err_t err = nvs_get_blob(nvs, name, out, &len);
    if (err == ESP_OK && len == KEYSTORE_KEY_SIZE) {
        return ESP_OK;
    }
    ESP_RETURN_ON_FALSE(err == ESP_ERR_NVS_NOT_FOUND, ESP_ERR_INVALID_STATE, TAG, "%s unreadable", name);
    esp_fill_random(out, KEYSTORE_KEY_SIZE);
    ESP_RETURN_ON_ERROR(nvs_set_blob(nvs, name, out, KEYSTORE_KEY_SIZE), TAG, "store %s", name);
    ESP_LOGI(TAG, "created %s", name);
    return nvs_commit(nvs);
}

static esp_err_t import_dev_key(const uint8_t root[KEYSTORE_KEY_SIZE])
{
    psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_usage_flags(&attr, PSA_KEY_USAGE_SIGN_MESSAGE);
    psa_set_key_algorithm(&attr, PSA_ALG_HMAC(PSA_ALG_SHA_256));
    psa_set_key_type(&attr, PSA_KEY_TYPE_HMAC);
    psa_status_t st = psa_import_key(&attr, root, KEYSTORE_KEY_SIZE, &s_dev_key);
    return st == PSA_SUCCESS ? ESP_OK : ESP_FAIL;
}

esp_err_t keystore_init(void)
{
    ESP_RETURN_ON_FALSE(psa_crypto_init() == PSA_SUCCESS, ESP_FAIL, TAG, "psa init");

    s_status.secure_boot = esp_secure_boot_enabled();
    s_status.flash_encryption = esp_flash_encryption_enabled();

    nvs_handle_t nvs;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs), TAG, "nvs open");
    esp_err_t err = load_or_create(nvs, NVS_SALT, s_salt);

    esp_efuse_block_t block;
    if (err == ESP_OK && esp_efuse_find_purpose(ESP_EFUSE_KEY_PURPOSE_HMAC_UP, &block)) {
        s_status.root = KEYSTORE_ROOT_EFUSE;
        s_efuse_key = (hmac_key_id_t)(block - EFUSE_BLK_KEY0);
    } else if (err == ESP_OK) {
        s_status.root = KEYSTORE_ROOT_DEV;
        uint8_t root[KEYSTORE_KEY_SIZE];
        err = load_or_create(nvs, NVS_ROOT, root);
        if (err == ESP_OK) {
            err = import_dev_key(root);
        }
        memset(root, 0, sizeof(root));
    }
    nvs_close(nvs);
    ESP_RETURN_ON_ERROR(err, TAG, "init");

    s_ready = true;
    if (keystore_is_secure()) {
        ESP_LOGI(TAG, "secure: eFuse root key, secure boot, flash encryption");
    } else {
        ESP_LOGW(TAG, "INSECURE: root=%s secure_boot=%d flash_encryption=%d",
                 s_status.root == KEYSTORE_ROOT_EFUSE ? "efuse" : "dev",
                 s_status.secure_boot, s_status.flash_encryption);
    }
    return ESP_OK;
}

void keystore_status(keystore_status_t *status)
{
    *status = s_status;
}

bool keystore_is_secure(void)
{
    return s_status.root == KEYSTORE_ROOT_EFUSE && s_status.secure_boot && s_status.flash_encryption;
}

esp_err_t keystore_derive(const char *label, const void *context, size_t context_len,
                          uint8_t out[KEYSTORE_KEY_SIZE])
{
    ESP_RETURN_ON_FALSE(s_ready, ESP_ERR_INVALID_STATE, TAG, "not initialised");
    size_t label_len = strlen(label);
    ESP_RETURN_ON_FALSE(label_len && label_len <= KEYSTORE_MAX_LABEL && context_len <= KEYSTORE_MAX_CONTEXT,
                        ESP_ERR_INVALID_ARG, TAG, "label/context size");

    uint8_t msg[KEYSTORE_MAX_LABEL + 1 + KEYSTORE_KEY_SIZE + KEYSTORE_MAX_CONTEXT];
    size_t len = 0;
    memcpy(msg, label, label_len);
    len += label_len;
    msg[len++] = 0;
    memcpy(msg + len, s_salt, KEYSTORE_KEY_SIZE);
    len += KEYSTORE_KEY_SIZE;
    if (context_len) {
        memcpy(msg + len, context, context_len);
        len += context_len;
    }

    esp_err_t err;
    if (s_status.root == KEYSTORE_ROOT_EFUSE) {
        err = esp_hmac_calculate(s_efuse_key, msg, len, out);
    } else {
        size_t out_len = 0;
        psa_status_t st = psa_mac_compute(s_dev_key, PSA_ALG_HMAC(PSA_ALG_SHA_256), msg, len,
                                          out, KEYSTORE_KEY_SIZE, &out_len);
        err = st == PSA_SUCCESS && out_len == KEYSTORE_KEY_SIZE ? ESP_OK : ESP_FAIL;
    }
    memset(msg, 0, sizeof(msg));
    return err;
}
