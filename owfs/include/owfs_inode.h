#ifndef OWFS_INODE_H
#define OWFS_INODE_H

#include "owfs_types.h"
#include "owfs_superblock.h"

#define OWFS_DIRECT_BLOCKS  10
#define OWFS_INDIRECT_BLOCK  1

typedef struct __attribute__((packed)) {
    uint32_t inode_number;              /* 0x00 */
    uint8_t  entry_type;                /* 0x04: OWFS_ENTRY_FILE / CATALOG / DELETED */
    uint8_t  name_length;               /* 0x05: SUTF-8 byte count */
    uint16_t permissions;               /* 0x06: 9-bit rwx mode */
    uint16_t flags;                     /* 0x08 */
    uint32_t size_bytes;                /* 0x0A: File size (0 for catalogs) */
    uint32_t block_count;               /* 0x0E: Allocated data block count */
    uint32_t created_timestamp;         /* 0x12: Epoch seconds */
    uint32_t modified_timestamp;        /* 0x16 */
    uint32_t parent_inode;              /* 0x1A: Parent catalog inode */
    uint32_t direct_blocks[OWFS_DIRECT_BLOCKS]; /* 0x1E: 10 * 4B = 40B */
    uint32_t indirect_block;            /* 0x46: Pointer to indirect block */
    uint8_t  name[OWFS_NAME_MAX_BYTES]; /* 0x4A: SUTF-8 name (128 bytes) */
    uint8_t  reserved[0xFC - 0xCA];     /* 0xCA: Pad toward checksum (50 bytes) */
    uint32_t checksum;                  /* 0xFC: CRC32c of this inode entry */
} owfs_inode_t;

uint32_t owfs_inode_compute_checksum(const owfs_inode_t *inode);
bool owfs_inode_verify_checksum(const owfs_inode_t *inode);
owfs_status_t owfs_inode_read(htl_device_t *dev, const owfs_superblock_t *sb, uint32_t inode_num, owfs_inode_t *inode);
owfs_status_t owfs_inode_write(htl_device_t *dev, const owfs_superblock_t *sb, uint32_t inode_num, owfs_inode_t *inode);

/* Allocate a new inode of the given type with SUTF-8 name and parent.
 * For OWFS_ENTRY_CATALOG the inode is created without a data block; the first
 * catalog insert allocates one lazily. */
owfs_status_t owfs_inode_alloc(htl_device_t *dev, owfs_superblock_t *sb,
                               uint8_t entry_type,
                               const uint8_t *name, size_t name_len,
                               uint32_t parent, uint32_t *out_inode_num);

/* Mark an inode deleted and release all of its data blocks. */
owfs_status_t owfs_inode_free(htl_device_t *dev, owfs_superblock_t *sb, uint32_t inode_num);

#endif /* OWFS_INODE_H */
