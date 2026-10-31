#!/usr/bin/env bash
# Fails if a resolved sdkconfig enables an option that burns eFuses, or if firmware sources call an
# eFuse-writing API.
#
#   tools/check_no_efuse.sh <sdkconfig>...
set -euo pipefail

options='CONFIG_(SECURE_BOOT|SECURE_FLASH_ENC_ENABLED|SECURE_DISABLE_ROM_DL_MODE|SECURE_ENABLE_SECURE_ROM_DL_MODE|NVS_ENCRYPTION|BOOTLOADER_APP_ANTI_ROLLBACK)=y'
calls='esp_efuse_(write|set|batch_write)|esp_ota_revoke|permanently_enable|flash_encryption_set_release_mode'

status=0
for config in "$@"; do
    [ -f "$config" ] || { echo "$config: missing" >&2; status=1; continue; }
    if grep -nE "^$options$" "$config"; then
        echo "$config enables an option that burns eFuses" >&2
        status=1
    fi
done
if grep -rnE "$calls" src components --include='*.c' --include='*.h' --exclude-dir=canokey-core; then
    echo "firmware code calls an eFuse-writing API" >&2
    status=1
fi
exit $status
