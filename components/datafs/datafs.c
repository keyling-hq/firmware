#include "datafs.h"
#include "esp_check.h"
#include "esp_littlefs.h"
#include "esp_log.h"
#include "esp_partition.h"

static const char *TAG = "datafs";

#define PARTITION_LABEL "storage"

esp_err_t datafs_mount(void)
{
    esp_vfs_littlefs_conf_t conf = {
        .base_path = DATAFS_BASE_PATH,
        .partition_label = PARTITION_LABEL,
        .format_if_mount_failed = true,
    };
    ESP_RETURN_ON_ERROR(esp_vfs_littlefs_register(&conf), TAG, "mount");

    /* littlefs takes its size from the superblock, so a shrunk partition still mounts at the old size. */
    const esp_partition_t *part =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, PARTITION_LABEL);
    size_t total = 0, used = 0;
    if (part && esp_littlefs_info(PARTITION_LABEL, &total, &used) == ESP_OK && total > part->size) {
        ESP_LOGW(TAG, "filesystem larger than its partition: formatting");
        ESP_RETURN_ON_ERROR(esp_littlefs_format(PARTITION_LABEL), TAG, "format");
    }
    if (esp_littlefs_info(PARTITION_LABEL, &total, &used) == ESP_OK) {
        ESP_LOGI(TAG, "mounted at %s: %u KB used of %u KB", DATAFS_BASE_PATH,
                 (unsigned)(used / 1024), (unsigned)(total / 1024));
    }
    return ESP_OK;
}

esp_err_t datafs_format(void)
{
    return esp_littlefs_format(PARTITION_LABEL);
}
