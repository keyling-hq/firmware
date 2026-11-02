#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * A read-only FAT16 disk built on the fly from a list of files: every block is generated when the
 * host reads it, so the disk costs no RAM beyond the files themselves.
 */

#define VDISK_BLOCK_SIZE    512

typedef struct {
    const char *name;   /* 8.3, e.g. "INFO.TXT"; shown in upper case */
    const void *data;
    uint32_t size;
} vdisk_file_t;

/** files must stay valid while the disk is in use. Files that don't fit are left out. */
void vdisk_set_files(const vdisk_file_t *files, size_t count);

uint32_t vdisk_block_count(void);

void vdisk_read(uint32_t lba, uint8_t block[VDISK_BLOCK_SIZE]);

#ifdef __cplusplus
}
#endif
