#include "datafs.h"
#include "esp_check.h"
#include "esp_littlefs.h"
#include "esp_log.h"

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

    size_t total = 0, used = 0;
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
