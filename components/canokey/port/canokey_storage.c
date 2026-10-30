#include <string.h>
#include "canokey_storage.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_random.h"
#include "keystore.h"
#include "nvs.h"
#include "aes/esp_aes.h"
#include <fs.h>
#include <lfs.h>

static const char *TAG = "canokey_fs";

#define FS_BLOCK_SIZE       4096
#define FS_CACHE_SIZE       256
#define FS_LOOKAHEAD_SIZE   32

#define NVS_NAMESPACE       "canokey"
#define NVS_PHONE_LOCKED_ID    "pid"
#define STORAGE_SALT_SIZE   16
#define XTS_UNIT            16

#define PGP_FILE_PREFIX     "pgp-"

enum { STORE_FIDO, STORE_PGP, STORE_COUNT };

typedef struct {
    const char *label;      /* partition and keystore label */
    const char *salt_name;  /* NVS key of the store's salt */
    const char *ready_name; /* phone-locked stores: NVS flag, set once formatted under a secret */
    const esp_partition_t *part;
    bool mounted;
    lfs_t lfs;
    struct lfs_config cfg;
    esp_aes_xts_context enc;
    esp_aes_xts_context dec;
    uint8_t read_buf[FS_CACHE_SIZE];
    uint8_t prog_buf[FS_CACHE_SIZE];
    uint8_t lookahead_buf[FS_LOOKAHEAD_SIZE];
} store_t;

static store_t s_stores[CANOKEY_PROFILE_COUNT][STORE_COUNT] = {
    [CANOKEY_PROFILE_MAIN] = {
        [STORE_FIDO] = { .label = "fido", .salt_name = "fssalt" },
        [STORE_PGP] = { .label = "openpgp", .salt_name = "pgpsalt" },
    },
    [CANOKEY_PROFILE_PHONE_LOCKED] = {
        [STORE_FIDO] = { .label = "fido_p", .salt_name = "fpsalt", .ready_name = "fpready" },
        [STORE_PGP] = { .label = "openpgp_p", .salt_name = "ppsalt", .ready_name = "ppready" },
    },
};
static canokey_profile_t s_active = CANOKEY_PROFILE_MAIN;
static uint8_t s_secret[CANOKEY_SECRET_SIZE];
static bool s_secret_set;
static uint8_t s_cipher_buf[FS_CACHE_SIZE];

/* The filesystem the generated fs.c operates on; calls without a path get the active FIDO store. */
lfs_t *canokey_fs_lfs = &s_stores[CANOKEY_PROFILE_MAIN][STORE_FIDO].lfs;

static lfs_t *active_fido_lfs(void)
{
    return &s_stores[s_active][STORE_FIDO].lfs;
}

static esp_err_t store_salt(store_t *st, bool renew, uint8_t salt[STORAGE_SALT_SIZE])
{
    nvs_handle_t nvs;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs), TAG, "nvs");
    size_t len = STORAGE_SALT_SIZE;
    esp_err_t err = ESP_OK;
    if (renew || nvs_get_blob(nvs, st->salt_name, salt, &len) != ESP_OK || len != STORAGE_SALT_SIZE) {
        esp_fill_random(salt, STORAGE_SALT_SIZE);
        err = nvs_set_blob(nvs, st->salt_name, salt, STORAGE_SALT_SIZE);
        if (err == ESP_OK) {
            err = nvs_commit(nvs);
        }
    }
    nvs_close(nvs);
    ESP_RETURN_ON_ERROR(err, TAG, "%s: salt", st->label);
    return ESP_OK;
}

static bool store_ready(const store_t *st)
{
    nvs_handle_t nvs;
    uint8_t ready = 0;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) == ESP_OK) {
        nvs_get_u8(nvs, st->ready_name, &ready);
        nvs_close(nvs);
    }
    return ready == 1;
}

static esp_err_t store_set_ready(const store_t *st, bool ready)
{
    nvs_handle_t nvs;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs), TAG, "nvs");
    esp_err_t err = ready ? nvs_set_u8(nvs, st->ready_name, 1) : nvs_erase_key(nvs, st->ready_name);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = ESP_OK;
    }
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    return err;
}

/*
 * Everything on a store's partition is encrypted with XTS-AES-128 in 16-byte units, tweaked by
 * the unit's offset: the scheme of the chip's flash encryption, keyed from the keystore. A new
 * keystore root, salt or secret makes all earlier contents unreadable.
 */
static esp_err_t store_key_load(store_t *st, bool renew_salt)
{
    uint8_t input[STORAGE_SALT_SIZE + CANOKEY_SECRET_SIZE];
    size_t input_len = STORAGE_SALT_SIZE;
    ESP_RETURN_ON_ERROR(store_salt(st, renew_salt, input), TAG, "%s: salt", st->label);
    if (st->ready_name) {
        ESP_RETURN_ON_FALSE(s_secret_set, ESP_ERR_INVALID_STATE, TAG, "%s: no secret", st->label);
        memcpy(input + STORAGE_SALT_SIZE, s_secret, CANOKEY_SECRET_SIZE);
        input_len += CANOKEY_SECRET_SIZE;
    }

    uint8_t key[KEYSTORE_KEY_SIZE];
    esp_err_t err = keystore_derive(st->label, input, input_len, key);
    memset(input, 0, sizeof(input));
    if (err == ESP_OK) {
        esp_aes_xts_free(&st->enc);
        esp_aes_xts_free(&st->dec);
        esp_aes_xts_init(&st->enc);
        esp_aes_xts_init(&st->dec);
        if (esp_aes_xts_setkey_enc(&st->enc, key, 8 * sizeof(key)) != 0 ||
            esp_aes_xts_setkey_dec(&st->dec, key, 8 * sizeof(key)) != 0) {
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
    store_t *st = c->context;
    uint32_t addr = block * c->block_size + off;
    if (esp_partition_read(st->part, addr, buf, size) != ESP_OK) {
        return LFS_ERR_IO;
    }
    return crypt_units(&st->dec, ESP_AES_DECRYPT, addr, buf, buf, size);
}

static int lfs_prog(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, const void *buf, lfs_size_t size)
{
    store_t *st = c->context;
    uint32_t addr = block * c->block_size + off;
    const uint8_t *in = buf;
    while (size) {
        size_t chunk = size < sizeof(s_cipher_buf) ? size : sizeof(s_cipher_buf);
        if (crypt_units(&st->enc, ESP_AES_ENCRYPT, addr, in, s_cipher_buf, chunk) != LFS_ERR_OK ||
            esp_partition_write(st->part, addr, s_cipher_buf, chunk) != ESP_OK) {
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
    store_t *st = c->context;
    return esp_partition_erase_range(st->part, block * c->block_size, c->block_size) == ESP_OK ? LFS_ERR_OK
                                                                                                : LFS_ERR_IO;
}

static int lfs_sync(const struct lfs_config *c)
{
    return LFS_ERR_OK;
}

static void store_unmount(store_t *st)
{
    if (st->mounted) {
        lfs_unmount(&st->lfs);
        st->mounted = false;
    }
}

/*
 * Mounts with the current key. A store that doesn't mount is formatted only if allowed: phone-locked
 * stores never are, since a wrong secret must not destroy them. littlefs's format rewrites only
 * its superblocks, so the partition is erased first and no old block survives.
 */
static esp_err_t store_mount(store_t *st, bool fresh, bool format_on_fail)
{
    canokey_fs_lfs = &st->lfs;
    int rc = fresh ? LFS_ERR_CORRUPT : fs_mount(&st->cfg);
    if (rc < 0 && (fresh || format_on_fail)) {
        if (!fresh) {
            ESP_LOGW(TAG, "formatting '%s'", st->label);
        }
        rc = esp_partition_erase_range(st->part, 0, st->part->size) == ESP_OK ? fs_format(&st->cfg) : LFS_ERR_IO;
        if (rc == 0) {
            rc = fs_mount(&st->cfg);
        }
    }
    canokey_fs_lfs = active_fido_lfs();
    st->mounted = rc == 0;
    return rc == 0 ? ESP_OK : ESP_FAIL;
}

static esp_err_t store_init(store_t *st)
{
    st->part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, st->label);
    ESP_RETURN_ON_FALSE(st->part, ESP_ERR_NOT_FOUND, TAG, "no '%s' partition", st->label);
    st->cfg = (struct lfs_config){
        .context = st,
        .read = lfs_read,
        .prog = lfs_prog,
        .erase = lfs_erase,
        .sync = lfs_sync,
        .read_size = 16,
        .prog_size = 16,
        .block_size = FS_BLOCK_SIZE,
        .block_count = st->part->size / FS_BLOCK_SIZE,
        .block_cycles = 500,
        .cache_size = FS_CACHE_SIZE,
        .lookahead_size = FS_LOOKAHEAD_SIZE,
        .read_buffer = st->read_buf,
        .prog_buffer = st->prog_buf,
        .lookahead_buffer = st->lookahead_buf,
    };
    return ESP_OK;
}

esp_err_t canokey_storage_start(void)
{
    for (int p = 0; p < CANOKEY_PROFILE_COUNT; p++) {
        for (int i = 0; i < STORE_COUNT; i++) {
            ESP_RETURN_ON_ERROR(store_init(&s_stores[p][i]), TAG, "init");
        }
    }
    for (int i = 0; i < STORE_COUNT; i++) {
        store_t *st = &s_stores[CANOKEY_PROFILE_MAIN][i];
        /* Without the key a failed mount must not lead to a format. */
        ESP_RETURN_ON_ERROR(store_key_load(st, false), TAG, "%s: key", st->label);
        /* Also the path after a factory reset: the new keystore salt changes the store's key. */
        ESP_RETURN_ON_ERROR(store_mount(st, false, true), TAG, "%s: mount", st->label);
    }
    return ESP_OK;
}

static bool profile_open(canokey_profile_t profile)
{
    return s_stores[profile][STORE_FIDO].mounted && s_stores[profile][STORE_PGP].mounted;
}

esp_err_t canokey_storage_select(canokey_profile_t profile)
{
    ESP_RETURN_ON_FALSE(profile < CANOKEY_PROFILE_COUNT && profile_open(profile), ESP_ERR_INVALID_STATE, TAG,
                        "profile %d not open", profile);
    s_active = profile;
    canokey_fs_lfs = active_fido_lfs();
    return ESP_OK;
}

canokey_profile_state_t canokey_storage_state(canokey_profile_t profile)
{
    if (profile_open(profile)) {
        return CANOKEY_PROFILE_OPEN;
    }
    for (int i = 0; i < STORE_COUNT; i++) {
        if (store_ready(&s_stores[profile][i])) {
            return CANOKEY_PROFILE_WAITING;
        }
    }
    return CANOKEY_PROFILE_NEW;
}

static void forget_secret(void)
{
    memset(s_secret, 0, sizeof(s_secret));
    s_secret_set = false;
}

static esp_err_t phone_locked_id_set(bool create)
{
    nvs_handle_t nvs;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs), TAG, "nvs");
    esp_err_t err;
    if (create) {
        uint8_t id[CANOKEY_PROFILE_ID_SIZE];
        esp_fill_random(id, sizeof(id));
        err = nvs_set_blob(nvs, NVS_PHONE_LOCKED_ID, id, sizeof(id));
    } else {
        err = nvs_erase_key(nvs, NVS_PHONE_LOCKED_ID);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            err = ESP_OK;
        }
    }
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    return err;
}

bool canokey_storage_phone_locked_id(uint8_t id[CANOKEY_PROFILE_ID_SIZE])
{
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK) {
        return false;
    }
    size_t len = CANOKEY_PROFILE_ID_SIZE;
    bool found = nvs_get_blob(nvs, NVS_PHONE_LOCKED_ID, id, &len) == ESP_OK && len == CANOKEY_PROFILE_ID_SIZE;
    nvs_close(nvs);
    return found;
}

esp_err_t canokey_storage_set_secret(const uint8_t *secret, size_t len)
{
    ESP_RETURN_ON_FALSE(len == CANOKEY_SECRET_SIZE, ESP_ERR_INVALID_SIZE, TAG, "secret size %u", (unsigned)len);
    if (s_secret_set) {
        return memcmp(s_secret, secret, len) == 0 ? ESP_OK : ESP_ERR_INVALID_ARG;
    }
    memcpy(s_secret, secret, len);
    s_secret_set = true;

    store_t *stores = s_stores[CANOKEY_PROFILE_PHONE_LOCKED];
    /* Stores already set up must all open before a new one is set up under this secret. */
    for (int i = 0; i < STORE_COUNT; i++) {
        store_t *st = &stores[i];
        if (store_ready(st) && (store_key_load(st, false) != ESP_OK || store_mount(st, false, false) != ESP_OK)) {
            ESP_LOGW(TAG, "'%s' doesn't open with this secret", st->label);
            for (int j = 0; j < STORE_COUNT; j++) {
                store_unmount(&stores[j]);
            }
            forget_secret();
            return ESP_ERR_INVALID_ARG;
        }
    }
    uint8_t id[CANOKEY_PROFILE_ID_SIZE];
    if (!canokey_storage_phone_locked_id(id)) {
        ESP_RETURN_ON_ERROR(phone_locked_id_set(true), TAG, "profile id");
    }
    for (int i = 0; i < STORE_COUNT; i++) {
        store_t *st = &stores[i];
        if (!st->mounted) {
            ESP_LOGI(TAG, "setting up '%s'", st->label);
            ESP_RETURN_ON_ERROR(store_key_load(st, true), TAG, "%s: key", st->label);
            ESP_RETURN_ON_ERROR(store_mount(st, true, true), TAG, "%s: format", st->label);
            ESP_RETURN_ON_ERROR(store_set_ready(st, true), TAG, "%s: flag", st->label);
        }
    }
    ESP_LOGI(TAG, "phone-locked profile open");
    return ESP_OK;
}

/* Erases a store under a new salt. Without the secret a phone-locked store is only erased, and set up again with it. */
static esp_err_t store_wipe(store_t *st)
{
    store_unmount(st);
    if (st->ready_name && !s_secret_set) {
        uint8_t salt[STORAGE_SALT_SIZE];
        ESP_RETURN_ON_ERROR(store_salt(st, true, salt), TAG, "%s: salt", st->label);
        ESP_RETURN_ON_ERROR(store_set_ready(st, false), TAG, "%s: flag", st->label);
        ESP_RETURN_ON_FALSE(esp_partition_erase_range(st->part, 0, st->part->size) == ESP_OK, ESP_FAIL, TAG,
                            "%s: erase", st->label);
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(store_key_load(st, true), TAG, "%s: key", st->label);
    return store_mount(st, true, true);
}

esp_err_t canokey_storage_wipe_fido(canokey_profile_t profile)
{
    return store_wipe(&s_stores[profile][STORE_FIDO]);
}

esp_err_t canokey_storage_wipe_profile(canokey_profile_t profile)
{
    if (profile == CANOKEY_PROFILE_PHONE_LOCKED) {
        forget_secret();
        ESP_RETURN_ON_ERROR(phone_locked_id_set(false), TAG, "profile id");
    }
    for (int i = 0; i < STORE_COUNT; i++) {
        ESP_RETURN_ON_ERROR(store_wipe(&s_stores[profile][i]), TAG, "wipe");
    }
    return ESP_OK;
}

/* ---- routing: canokey's file functions (linked with -Wl,--wrap) ----------- */

static lfs_t *route(const char *path)
{
    bool pgp = strncmp(path, PGP_FILE_PREFIX, strlen(PGP_FILE_PREFIX)) == 0;
    store_t *st = &s_stores[s_active][pgp ? STORE_PGP : STORE_FIDO];
    return st->mounted ? &st->lfs : NULL;
}

#define ROUTED(path, call)                  \
    do {                                    \
        lfs_t *lfs_ = route(path);          \
        if (!lfs_) {                        \
            return LFS_ERR_IO;              \
        }                                   \
        canokey_fs_lfs = lfs_;              \
        int rc_ = (call);                   \
        canokey_fs_lfs = active_fido_lfs(); \
        return rc_;                         \
    } while (0)

int __real_read_file(const char *path, void *buf, lfs_soff_t off, lfs_size_t len);
int __real_write_file(const char *path, const void *buf, lfs_soff_t off, lfs_size_t len, uint8_t trunc);
int __real_append_file(const char *path, const void *buf, lfs_size_t len);
int __real_truncate_file(const char *path, lfs_size_t len);
int __real_write_file_attrs(const char *path, const struct lfs_attr *attrs, int attr_count, const void *buf,
                            lfs_size_t len, uint8_t trunc);
int __real_set_attrs_commit(const char *path, const struct lfs_attr *attrs, int attr_count);
int __real_read_attr(const char *path, uint8_t attr, void *buf, lfs_size_t len);
int __real_write_attr(const char *path, uint8_t attr, const void *buf, lfs_size_t len);
int __real_remove_attr(const char *path, uint8_t attr);
int __real_get_file_size(const char *path);
int __real_get_attr_size(const char *path, uint8_t attr);
int __real_fs_rename(const char *old, const char *new);
int __real_remove_file(const char *path);

int __wrap_read_file(const char *path, void *buf, lfs_soff_t off, lfs_size_t len)
{
    ROUTED(path, __real_read_file(path, buf, off, len));
}

int __wrap_write_file(const char *path, const void *buf, lfs_soff_t off, lfs_size_t len, uint8_t trunc)
{
    ROUTED(path, __real_write_file(path, buf, off, len, trunc));
}

int __wrap_append_file(const char *path, const void *buf, lfs_size_t len)
{
    ROUTED(path, __real_append_file(path, buf, len));
}

int __wrap_truncate_file(const char *path, lfs_size_t len)
{
    ROUTED(path, __real_truncate_file(path, len));
}

int __wrap_write_file_attrs(const char *path, const struct lfs_attr *attrs, int attr_count, const void *buf,
                            lfs_size_t len, uint8_t trunc)
{
    ROUTED(path, __real_write_file_attrs(path, attrs, attr_count, buf, len, trunc));
}

int __wrap_set_attrs_commit(const char *path, const struct lfs_attr *attrs, int attr_count)
{
    ROUTED(path, __real_set_attrs_commit(path, attrs, attr_count));
}

int __wrap_read_attr(const char *path, uint8_t attr, void *buf, lfs_size_t len)
{
    ROUTED(path, __real_read_attr(path, attr, buf, len));
}

int __wrap_write_attr(const char *path, uint8_t attr, const void *buf, lfs_size_t len)
{
    ROUTED(path, __real_write_attr(path, attr, buf, len));
}

int __wrap_remove_attr(const char *path, uint8_t attr)
{
    ROUTED(path, __real_remove_attr(path, attr));
}

int __wrap_get_file_size(const char *path)
{
    ROUTED(path, __real_get_file_size(path));
}

int __wrap_get_attr_size(const char *path, uint8_t attr)
{
    ROUTED(path, __real_get_attr_size(path, attr));
}

/* Both names are in the same store: canokey only renames a file to its applet's own names. */
int __wrap_fs_rename(const char *old, const char *new)
{
    ROUTED(old, __real_fs_rename(old, new));
}

int __wrap_remove_file(const char *path)
{
    ROUTED(path, __real_remove_file(path));
}
