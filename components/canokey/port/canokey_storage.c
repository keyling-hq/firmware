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
#define STORAGE_SALT_SIZE   16
#define XTS_UNIT            16

#define PGP_FILE_PREFIX     "pgp-"

typedef struct {
    const char *label;      /* partition and keystore label */
    const char *salt_name;  /* NVS key of the store's salt */
    const esp_partition_t *part;
    lfs_t lfs;
    struct lfs_config cfg;
    esp_aes_xts_context enc;
    esp_aes_xts_context dec;
    uint8_t read_buf[FS_CACHE_SIZE];
    uint8_t prog_buf[FS_CACHE_SIZE];
    uint8_t lookahead_buf[FS_LOOKAHEAD_SIZE];
} store_t;

static store_t s_fido = { .label = "fido", .salt_name = "fssalt" };
static store_t s_pgp = { .label = "openpgp", .salt_name = "pgpsalt" };
static uint8_t s_cipher_buf[FS_CACHE_SIZE];

/* The filesystem the generated fs.c operates on; calls without a path always get FIDO's. */
lfs_t *canokey_fs_lfs = &s_fido.lfs;

/*
 * Everything on a store's partition is encrypted with XTS-AES-128 in 16-byte units, tweaked by
 * the unit's offset: the scheme of the chip's flash encryption, keyed from the keystore. A new
 * keystore root or salt makes all earlier contents unreadable.
 */
static esp_err_t store_key_load(store_t *st, bool renew)
{
    uint8_t salt[STORAGE_SALT_SIZE];
    nvs_handle_t nvs;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs), TAG, "nvs");
    size_t len = sizeof(salt);
    esp_err_t err = ESP_OK;
    if (renew || nvs_get_blob(nvs, st->salt_name, salt, &len) != ESP_OK || len != sizeof(salt)) {
        esp_fill_random(salt, sizeof(salt));
        err = nvs_set_blob(nvs, st->salt_name, salt, sizeof(salt));
        if (err == ESP_OK) {
            err = nvs_commit(nvs);
        }
    }
    nvs_close(nvs);
    ESP_RETURN_ON_ERROR(err, TAG, "%s: salt", st->label);

    uint8_t key[KEYSTORE_KEY_SIZE];
    err = keystore_derive(st->label, salt, sizeof(salt), key);
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

/* littlefs's format rewrites only its superblocks: the rest is erased so no old block survives. */
static int store_format(store_t *st)
{
    if (esp_partition_erase_range(st->part, 0, st->part->size) != ESP_OK) {
        return LFS_ERR_IO;
    }
    return fs_format(&st->cfg);
}

static esp_err_t store_mount(store_t *st, bool fresh)
{
    canokey_fs_lfs = &st->lfs;
    int rc = fresh ? LFS_ERR_CORRUPT : fs_mount(&st->cfg);
    if (rc < 0) {
        /* Also the path after a factory reset: the new keystore salt changes the store's key. */
        ESP_LOGW(TAG, "formatting '%s'", st->label);
        rc = store_format(st);
        if (rc == 0) {
            rc = fs_mount(&st->cfg);
        }
    }
    canokey_fs_lfs = &s_fido.lfs;
    ESP_RETURN_ON_FALSE(rc == 0, ESP_FAIL, TAG, "%s: mount %d", st->label, rc);
    return ESP_OK;
}

static esp_err_t store_start(store_t *st)
{
    st->part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, st->label);
    ESP_RETURN_ON_FALSE(st->part, ESP_ERR_NOT_FOUND, TAG, "no '%s' partition", st->label);
    /* Without the key a failed mount must not lead to a format. */
    ESP_RETURN_ON_ERROR(store_key_load(st, false), TAG, "%s: key", st->label);

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
    ESP_RETURN_ON_ERROR(store_mount(st, false), TAG, "%s: mount", st->label);
    ESP_LOGI(TAG, "'%s': %u KB", st->label, (unsigned)(st->part->size / 1024));
    return ESP_OK;
}

esp_err_t canokey_storage_start(void)
{
    ESP_RETURN_ON_ERROR(store_start(&s_fido), TAG, "fido");
    return store_start(&s_pgp);
}

esp_err_t canokey_storage_wipe_fido(void)
{
    lfs_unmount(&s_fido.lfs);
    ESP_RETURN_ON_ERROR(store_key_load(&s_fido, true), TAG, "fido: key");
    return store_mount(&s_fido, true);
}

/* ---- routing: canokey's file functions (linked with -Wl,--wrap) ----------- */

static lfs_t *route(const char *path)
{
    return strncmp(path, PGP_FILE_PREFIX, strlen(PGP_FILE_PREFIX)) == 0 ? &s_pgp.lfs : &s_fido.lfs;
}

#define ROUTED(path, call)                  \
    do {                                    \
        canokey_fs_lfs = route(path);       \
        int rc_ = (call);                   \
        canokey_fs_lfs = &s_fido.lfs;       \
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
