#ifndef USFS_ENTRY_H
#define USFS_ENTRY_H

#include "usfs_types.h"
#include "usfs_superblock.h"

typedef struct __attribute__((packed)) {
    uint32_t entry_index;               /* 0x00 */
    uint8_t  entry_type;                /* 0x04: USFS_ENTRY_FILE / CATALOG / DELETED */
    uint8_t  security_flags;            /* 0x05 */
    uint8_t  permissions;               /* 0x06 */
    uint8_t  name_length;               /* 0x07 */
    uint32_t size_bytes;                /* 0x08 */
    uint32_t first_block;               /* 0x0C: Start of data chain */
    uint32_t block_count;               /* 0x10 */
    uint32_t parent_entry;              /* 0x14: Parent catalog index */
    uint32_t created_timestamp;         /* 0x18 */
    uint32_t modified_timestamp;        /* 0x1C */
    uint8_t  name[USFS_NAME_MAX_BYTES]; /* 0x20: SUTF-8 name (128 bytes) */
    uint8_t  reserved[0xFC - 0xA0];     /* 0xA0: Pad toward checksum (92 bytes) */
    uint32_t checksum;                  /* 0xFC: CRC32c of this entry */
} usfs_entry_t;

uint32_t usfs_entry_compute_checksum(const usfs_entry_t *entry);
bool usfs_entry_verify_checksum(const usfs_entry_t *entry);
usfs_status_t usfs_entry_read(htl_device_t *dev, const usfs_superblock_t *sb, uint32_t entry_idx, usfs_entry_t *entry);
usfs_status_t usfs_entry_write(htl_device_t *dev, const usfs_superblock_t *sb, uint32_t entry_idx, usfs_entry_t *entry);
usfs_status_t usfs_entry_alloc(htl_device_t *dev, usfs_superblock_t *sb, uint32_t *out_entry_idx);
usfs_status_t usfs_entry_free(htl_device_t *dev, usfs_superblock_t *sb, uint32_t entry_idx);
usfs_status_t usfs_entry_lookup(htl_device_t *dev, const usfs_superblock_t *sb, uint32_t parent_idx, const uint8_t *name, size_t name_len, uint32_t *out_entry_idx);

#endif /* USFS_ENTRY_H */
