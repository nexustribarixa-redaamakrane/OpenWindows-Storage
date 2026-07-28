#ifndef OWFS_SUPERBLOCK_H
#define OWFS_SUPERBLOCK_H

#include "owfs_types.h"

typedef struct __attribute__((packed)) {
    uint32_t magic;                     /* 0x00: OWFS_MAGIC */
    uint16_t version_major;             /* 0x04 */
    uint16_t version_minor;             /* 0x06 */
    uint32_t block_size;                /* 0x08: 0x1000 */
    uint32_t total_blocks;              /* 0x0C */
    uint32_t free_blocks;               /* 0x10 */
    uint32_t total_inodes;              /* 0x14 */
    uint32_t free_inodes;               /* 0x18 */
    uint32_t bitmap_start_block;        /* 0x1C: First block of allocation bitmap */
    uint32_t bitmap_block_count;        /* 0x20: Blocks consumed by bitmap */
    uint32_t inode_table_start;         /* 0x24: First block of inode table */
    uint32_t inode_table_blocks;        /* 0x28 */
    uint32_t data_region_start;         /* 0x2C: First data block */
    uint32_t root_inode;                /* 0x30: Always 0 */
    uint32_t mount_count;               /* 0x34 */
    uint32_t state_flags;               /* 0x38: OWFS_STATE_CLEAN/DIRTY/ERROR/LOCKED */
    uint64_t fletcher64_bitmap;         /* 0x3C: Fletcher-64 of bitmap region */
    uint8_t  volume_label[64];          /* 0x44: SUTF-8 volume label */
    uint32_t checksum;                  /* 0x84: CRC32c of this superblock */
    uint8_t  reserved[0x1000 - 0x88];   /* 0x88: Pad to 4096 bytes */
} owfs_superblock_t;

uint32_t owfs_superblock_compute_checksum(const owfs_superblock_t *sb);
bool owfs_superblock_validate(const owfs_superblock_t *sb);
owfs_status_t owfs_superblock_read(htl_device_t *dev, owfs_superblock_t *sb);
owfs_status_t owfs_superblock_write(htl_device_t *dev, owfs_superblock_t *sb);

#endif /* OWFS_SUPERBLOCK_H */
