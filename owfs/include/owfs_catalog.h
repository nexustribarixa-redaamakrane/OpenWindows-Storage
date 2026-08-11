#ifndef OWFS_CATALOG_H
#define OWFS_CATALOG_H

#include "owfs_types.h"
#include "owfs_superblock.h"
#include "owfs_inode.h"

typedef struct __attribute__((packed)) {
    uint32_t inode_number;              /* 0x00: Target inode */
    uint8_t  entry_type;                /* 0x04: OWFS_ENTRY_FILE / CATALOG / DELETED */
    uint8_t  name_length;               /* 0x05: SUTF-8 byte count */
    uint8_t  name[OWFS_NAME_MAX_BYTES]; /* 0x06: SUTF-8 encoded name */
    uint8_t  reserved[0xFC - 0x86];     /* 0x86: Pad toward checksum (118 bytes) */
    uint32_t checksum;                  /* 0xFC: CRC32c of this catalog entry */
} owfs_catalog_entry_t;

uint32_t owfs_catalog_entry_compute_checksum(const owfs_catalog_entry_t *entry);
bool owfs_catalog_entry_verify_checksum(const owfs_catalog_entry_t *entry);
owfs_status_t owfs_catalog_lookup(htl_device_t *dev, const owfs_superblock_t *sb, uint32_t catalog_inode, const uint8_t *name, size_t name_len, uint32_t *out_inode);
owfs_status_t owfs_catalog_insert(htl_device_t *dev, owfs_superblock_t *sb, uint32_t catalog_inode, const uint8_t *name, size_t name_len, uint32_t target_inode, uint8_t entry_type);
owfs_status_t owfs_catalog_remove(htl_device_t *dev, owfs_superblock_t *sb, uint32_t catalog_inode, const uint8_t *name, size_t name_len);
owfs_status_t owfs_catalog_list(htl_device_t *dev, const owfs_superblock_t *sb, uint32_t catalog_inode, owfs_catalog_entry_t *entries, uint32_t max_entries, uint32_t *out_count);

/* Create a new regular file in `parent_inode`, allocating a file inode and
 * linking it into the parent catalog. */
owfs_status_t owfs_catalog_create(htl_device_t *dev, owfs_superblock_t *sb,
                                  uint32_t parent_inode,
                                  const uint8_t *name, size_t name_len,
                                  uint32_t *out_inode);

/* Create a new sub-catalog in `parent_inode`, allocating a catalog-typed
 * inode and linking it into the parent catalog. */
owfs_status_t owfs_catalog_mkdir(htl_device_t *dev, owfs_superblock_t *sb,
                                 uint32_t parent_inode,
                                 const uint8_t *name, size_t name_len,
                                 uint32_t *out_inode);

#endif /* OWFS_CATALOG_H */
