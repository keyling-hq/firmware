#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Firmware identity: the git version plus the first 8 hex digits of the ELF's SHA-256,
 * e.g. "7888154-dirty+3f9a1c2e". The hash tells apart builds that share a git version.
 */
const char *fw_info_version(void);

#ifdef __cplusplus
}
#endif
