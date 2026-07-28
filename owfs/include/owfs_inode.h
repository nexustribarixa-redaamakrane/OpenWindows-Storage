#ifndef OWFS_INODE_H
#define OWFS_INODE_H

#include "owfs_types.h"
#include "owfs_superblock.h"

#define OWFS_DIRECT_BLOCKS  10
#define OWFS_INDIRECT_BLOCK  1

typedef struct __attribute__((packed)) {
    uint32_t inode_number;              /* 0x00 */
    uint8_t  entry_type;                /* 0x04: OWFS_ENTRY_FILE / CATALOG / DELETED */
    uint8_t  permissions;               /* 0x05: rwx bits */
    uint16_t flags;                     /* 0x06 */
    uint32_t size_bytes;                /* 0x08: File size (0 for catalogs) */
    uint32_t block_count;               /* 0x0C: Allocated block count */
    uint32_t created_timestamp;         /* 0x10: Epoch seconds */
    uint32_t modified_timestamp;        /* 0x14 */
    uint32_t parent_inode;              /* 0x18: Parent catalog inode */
    uint32_t direct_blocks[OWFS_DIRECT_BLOCKS]; /* 0x1C: 10 * 4B = 40B */
    uint32_t indirect_block;            /* 0x44: Pointer to indirect block */
    uint8_t  name[OWFS_NAME_MAX_BYTES]; /* 0x48: SUTF-8 name (128 bytes) */
    uint8_t  name_length;               /* 0xC8: Actual bytes in name[] */
    uint8_t  reserved[0xFC - 0xC9];     /* 0xC9: Pad toward checksum (51 bytes) */
    uint32_t checksum;                  /* 0xFC: CRC32c of this inode entry */
} owfs_inode_t;

uint32_t owfs_inode_compute_checksum(const owfs_inode_t *inode);
bool owfs_inode_verify_checksum(const owfs_inode_t *inode);
owfs_status_t owfs_inode_read(htl_device_t *dev, const owfs_superblock_t *sb, uint32_t inode_num, owfs_inode_t *inode);
owfs_status_t owfs_inode_write(htl_device_t *dev, const owfs_superblock_t *sb, uint32_t inode_num, owfs_inode_t *inode);
owfs_status_t owfs_inode_alloc(htl_device_t *dev, owfs_superblock_t *sb, uint32_t *out_inode_num);
owfs_status_t owfs_inode_free(htl_device_t *dev, owfs_superblock_t *sb, uint32_t inode_num);

#endif /* OWFS_INODE_H */
