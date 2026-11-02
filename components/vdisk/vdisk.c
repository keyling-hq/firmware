#include <ctype.h>
#include <string.h>
#include "vdisk.h"

/* FAT16 needs at least 4085 clusters: one block per cluster, 8 MB in all. */
#define BLOCK_COUNT     16384
#define FAT_COUNT       2
#define FAT_BLOCKS      64
#define ROOT_ENTRIES    64
#define ROOT_BLOCKS     (ROOT_ENTRIES * 32 / VDISK_BLOCK_SIZE)
#define FAT_START       1
#define ROOT_START      (FAT_START + FAT_COUNT * FAT_BLOCKS)
#define DATA_START      (ROOT_START + ROOT_BLOCKS)
#define CLUSTER_COUNT   (BLOCK_COUNT - DATA_START)
#define FIRST_CLUSTER   2
#define MAX_FILES       8

#define ATTR_READ_ONLY  0x01
#define ATTR_VOLUME_ID  0x08
/* 2026-01-01 00:00 */
#define FAT_DATE        (((2026 - 1980) << 9) | (1 << 5) | 1)

#define LABEL           "KEYLING    "
#define LABEL_LEN       11

typedef struct {
    char name[11];
    const uint8_t *data;
    uint32_t size;
    uint32_t cluster;   /* first one; 0 for an empty file */
    uint32_t clusters;
} entry_t;

static entry_t s_entries[MAX_FILES];
static size_t s_count;

static void put16(uint8_t *p, uint16_t v)
{
    p[0] = v & 0xFF;
    p[1] = v >> 8;
}

static void put32(uint8_t *p, uint32_t v)
{
    put16(p, v & 0xFFFF);
    put16(p + 2, v >> 16);
}

static void short_name(char out[11], const char *name)
{
    memset(out, ' ', 11);
    const char *dot = strrchr(name, '.');
    size_t base = dot ? (size_t)(dot - name) : strlen(name);
    for (size_t i = 0; i < base && i < 8; i++) {
        out[i] = (char)toupper((unsigned char)name[i]);
    }
    for (size_t i = 0; dot && dot[1 + i] && i < 3; i++) {
        out[8 + i] = (char)toupper((unsigned char)dot[1 + i]);
    }
}

void vdisk_set_files(const vdisk_file_t *files, size_t count)
{
    s_count = 0;
    uint32_t next = FIRST_CLUSTER;
    for (size_t i = 0; i < count && s_count < MAX_FILES; i++) {
        uint32_t clusters = (files[i].size + VDISK_BLOCK_SIZE - 1) / VDISK_BLOCK_SIZE;
        if (next + clusters > FIRST_CLUSTER + CLUSTER_COUNT) {
            continue;
        }
        entry_t *e = &s_entries[s_count++];
        short_name(e->name, files[i].name);
        e->data = files[i].data;
        e->size = files[i].size;
        e->cluster = clusters ? next : 0;
        e->clusters = clusters;
        next += clusters;
    }
}

uint32_t vdisk_block_count(void)
{
    return BLOCK_COUNT;
}

static void boot_block(uint8_t *b)
{
    static const uint8_t jump[3] = { 0xEB, 0x3C, 0x90 };
    memcpy(b, jump, sizeof(jump));
    memcpy(b + 3, "MSWIN4.1", 8);
    put16(b + 11, VDISK_BLOCK_SIZE);
    b[13] = 1;                          /* blocks per cluster */
    put16(b + 14, FAT_START);           /* reserved blocks */
    b[16] = FAT_COUNT;
    put16(b + 17, ROOT_ENTRIES);
    put16(b + 19, BLOCK_COUNT);
    b[21] = 0xF8;                       /* fixed disk */
    put16(b + 22, FAT_BLOCKS);
    put16(b + 24, 1);                   /* blocks per track */
    put16(b + 26, 1);                   /* heads */
    b[36] = 0x80;                       /* drive number */
    b[38] = 0x29;                       /* extended boot signature */
    put32(b + 39, 0x4B4C4E47);          /* volume serial */
    memcpy(b + 43, LABEL, LABEL_LEN);
    memcpy(b + 54, "FAT16   ", 8);
    b[510] = 0x55;
    b[511] = 0xAA;
}

static uint16_t fat_entry(uint32_t cluster)
{
    if (cluster < FIRST_CLUSTER) {
        return cluster == 0 ? 0xFFF8 : 0xFFFF;
    }
    for (size_t i = 0; i < s_count; i++) {
        const entry_t *e = &s_entries[i];
        if (e->clusters && cluster >= e->cluster && cluster < e->cluster + e->clusters) {
            return cluster == e->cluster + e->clusters - 1 ? 0xFFFF : (uint16_t)(cluster + 1);
        }
    }
    return 0;
}

static void fat_block(uint8_t *b, uint32_t index)
{
    uint32_t first = index * (VDISK_BLOCK_SIZE / 2);
    for (uint32_t i = 0; i < VDISK_BLOCK_SIZE / 2; i++) {
        put16(b + 2 * i, fat_entry(first + i));
    }
}

static void dir_entry(uint8_t *d, const char name[11], uint8_t attr, uint32_t cluster, uint32_t size)
{
    memcpy(d, name, 11);
    d[11] = attr;
    put16(d + 16, FAT_DATE);            /* created */
    put16(d + 18, FAT_DATE);            /* accessed */
    put16(d + 24, FAT_DATE);            /* written */
    put16(d + 26, cluster);
    put32(d + 28, size);
}

static void root_block(uint8_t *b, uint32_t index)
{
    for (uint32_t i = 0; i < VDISK_BLOCK_SIZE / 32; i++) {
        uint32_t slot = index * (VDISK_BLOCK_SIZE / 32) + i;
        if (slot == 0) {
            dir_entry(b + 32 * i, LABEL, ATTR_VOLUME_ID, 0, 0);
        } else if (slot - 1 < s_count) {
            const entry_t *e = &s_entries[slot - 1];
            dir_entry(b + 32 * i, e->name, ATTR_READ_ONLY, e->cluster, e->size);
        }
    }
}

static void data_block(uint8_t *b, uint32_t cluster)
{
    for (size_t i = 0; i < s_count; i++) {
        const entry_t *e = &s_entries[i];
        if (e->clusters && cluster >= e->cluster && cluster < e->cluster + e->clusters) {
            uint32_t offset = (cluster - e->cluster) * VDISK_BLOCK_SIZE;
            uint32_t len = e->size - offset < VDISK_BLOCK_SIZE ? e->size - offset : VDISK_BLOCK_SIZE;
            memcpy(b, e->data + offset, len);
            return;
        }
    }
}

void vdisk_read(uint32_t lba, uint8_t block[VDISK_BLOCK_SIZE])
{
    memset(block, 0, VDISK_BLOCK_SIZE);
    if (lba == 0) {
        boot_block(block);
    } else if (lba < ROOT_START) {
        fat_block(block, (lba - FAT_START) % FAT_BLOCKS);
    } else if (lba < DATA_START) {
        root_block(block, lba - ROOT_START);
    } else if (lba < BLOCK_COUNT) {
        data_block(block, lba - DATA_START + FIRST_CLUSTER);
    }
}
