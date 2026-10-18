#include <stdio.h>
#include "fw_info.h"
#include "esp_app_desc.h"

#define BUILD_ID_DIGITS 8

const char *fw_info_version(void)
{
    static char version[sizeof(((esp_app_desc_t *)0)->version) + 1 + BUILD_ID_DIGITS];
    if (!version[0]) {
        char build_id[BUILD_ID_DIGITS + 1];
        esp_app_get_elf_sha256(build_id, sizeof(build_id));
        snprintf(version, sizeof(version), "%s+%s", esp_app_get_description()->version, build_id);
    }
    return version;
}
