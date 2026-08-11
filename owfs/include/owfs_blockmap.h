#ifndef OWFS_BLOCKMAP_H
#define OWFS_BLOCKMAP_H

#include "owfs_types.h"
#include "owfs_inode.h"

/* Maximum number of addressable data blocks per inode:
 * 10 direct + 1024 single-indirect. */
#define OWFS_MAX_LOGICAL_BLOCKS (OWFS_DIRECT_BLOCKS + OWFS_INDIRECT_PTRS)

/* Translate a logical block index to its physical block number (read-only).
 * Index must be < inode->block_count. */
owfs_status_t owfs_blockmap_get(htl_device_t *dev, const owfs_inode_t *inode,
                                uint32_t idx, uint32_t *out_block);

/* Ensure a physical block exists for the logical index, allocating a data
 * block (and the indirect block when crossing the direct/indirect boundary).
 * The returned block is zeroed before returning. Does not persist the inode;
 * the caller must owfs_inode_write afterwards. */
owfs_status_t owfs_blockmap_ensure(htl_device_t *dev, owfs_superblock_t *sb,
                                   owfs_inode_t *inode, uint32_t idx, uint32_t *out_block);

/* Release the data blocks at logical indices [from_idx, block_count) and the
 * indirect block if it becomes unreferenced. Does not persist the inode; the
 * caller must owfs_inode_write afterwards. */
owfs_status_t owfs_blockmap_release(htl_device_t *dev, owfs_superblock_t *sb,
                                    owfs_inode_t *inode, uint32_t from_idx);

#endif /* OWFS_BLOCKMAP_H */
