#ifndef USFS_SUPERBLOCK_H
#define USFS_SUPERBLOCK_H

#include "usfs_types.h"

typedef struct __attribute__((packed)) {
    uint32_t magic;                     /* 0x00: USFS_MAGIC */
    uint16_t version_major;             /* 0x04 */
    uint16_t version_minor;             /* 0x06 */
    uint32_t block_size;                /* 0x08: 0x1000 */
    uint32_t total_blocks;              /* 0x0C */
    uint32_t free_blocks;               /* 0x10 */
    uint32_t total_entries;             /* 0x14 */
    uint32_t used_entries;              /* 0x18 */
    uint32_t entry_table_start;         /* 0x1C: Block of first entry table */
    uint32_t entry_table_blocks;        /* 0x20 */
    uint32_t bitmap_start_block;        /* 0x24: First block of free-block bitmap */
    uint32_t bitmap_block_count;        /* 0x28 */
    uint32_t data_region_start;         /* 0x2C */
    uint32_t security_flags;            /* 0x30: USFS_SEC_* */
    uint32_t state_flags;               /* 0x34: USFS_STATE_* */
    uint32_t mount_count;               /* 0x38 */
    uint32_t signature;                 /* 0x3C: CRC32c of entry table (SEC_SIGNED) */
    uint64_t fletcher64_bitmap;         /* 0x40: Fletcher-64 of bitmap region */
    uint8_t  volume_label[64];          /* 0x48: SUTF-8 volume label */
    uint8_t  key_slot_1[USFS_KEY_SLOT_SIZE]; /* 0x88: ChaCha20 key slot 1 (256B) */
    uint8_t  key_slot_2[USFS_KEY_SLOT_SIZE]; /* 0x188: ChaCha20 key slot 2 (256B) */
    uint32_t checksum;                  /* 0x288: CRC32c of this superblock */
    uint8_t  reserved[0x1000 - 0x28C];  /* 0x28C: Pad to 4096 bytes */
} usfs_superblock_t;

uint32_t usfs_superblock_compute_checksum(const usfs_superblock_t *sb);
bool usfs_superblock_validate(const usfs_superblock_t *sb);
usfs_status_t usfs_superblock_read(htl_device_t *dev, usfs_superblock_t *sb);
usfs_status_t usfs_superblock_write(htl_device_t *dev, usfs_superblock_t *sb);
uint32_t usfs_signature_compute(htl_device_t *dev, const usfs_superblock_t *sb);
usfs_status_t usfs_signature_update(htl_device_t *dev, usfs_superblock_t *sb);

#endif /* USFS_SUPERBLOCK_H */
