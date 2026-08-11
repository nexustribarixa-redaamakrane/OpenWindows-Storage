#ifndef OWFS_FILE_H
#define OWFS_FILE_H

#include "owfs_types.h"
#include "owfs_superblock.h"
#include "owfs_inode.h"

/* Read up to `len` bytes from a file inode starting at `offset`.
 * `out_read` receives the number of bytes actually read (may be short at EOF). */
owfs_status_t owfs_file_read(htl_device_t *dev, const owfs_superblock_t *sb,
                             const owfs_inode_t *inode,
                             uint8_t *buf, uint32_t offset, uint32_t len,
                             uint32_t *out_read);

/* Write `len` bytes into a file inode at `offset`, growing the file and
 * allocating blocks as needed. Persists the updated inode. */
owfs_status_t owfs_file_write(htl_device_t *dev, owfs_superblock_t *sb,
                              owfs_inode_t *inode, uint32_t inode_num,
                              const uint8_t *buf, uint32_t offset, uint32_t len,
                              uint32_t *out_written);

/* Truncate a file to `new_size` bytes, freeing released blocks. Persists the
 * updated inode. */
owfs_status_t owfs_file_truncate(htl_device_t *dev, owfs_superblock_t *sb,
                                 owfs_inode_t *inode, uint32_t inode_num,
                                 uint32_t new_size);

#endif /* OWFS_FILE_H */
