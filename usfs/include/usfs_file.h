#ifndef USFS_FILE_H
#define USFS_FILE_H

#include "usfs_types.h"
#include "usfs_superblock.h"
#include "usfs_entry.h"

/* Read up to `len` bytes from an entry starting at `offset`. */
usfs_status_t usfs_file_read(htl_device_t *dev, const usfs_superblock_t *sb,
                             const usfs_entry_t *entry,
                             uint8_t *buf, uint32_t offset, uint32_t len,
                             uint32_t *out_read);

/* Write `len` bytes into an entry at `offset`, growing the file and
 * allocating a contiguous data run as needed. Persists the updated entry. */
usfs_status_t usfs_file_write(htl_device_t *dev, usfs_superblock_t *sb,
                              usfs_entry_t *entry, uint32_t entry_idx,
                              const uint8_t *buf, uint32_t offset, uint32_t len,
                              uint32_t *out_written);

/* Truncate an entry to `new_size` bytes, freeing released blocks. */
usfs_status_t usfs_file_truncate(htl_device_t *dev, usfs_superblock_t *sb,
                                 usfs_entry_t *entry, uint32_t entry_idx,
                                 uint32_t new_size);

#endif /* USFS_FILE_H */
