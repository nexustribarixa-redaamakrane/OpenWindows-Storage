#ifndef OWFS_SYNC_H
#define OWFS_SYNC_H

#include "owfs_types.h"
#include "owfs_superblock.h"

/* Returns OWFS_ERR_VOLUME_DIRTY while the volume is locked or in an error
 * state (writes must not proceed until a consistency check passes). */
owfs_status_t owfs_volume_writable(const owfs_superblock_t *sb);

owfs_status_t owfs_mark_dirty(htl_device_t *dev, owfs_superblock_t *sb);
owfs_status_t owfs_mark_clean(htl_device_t *dev, owfs_superblock_t *sb);
bool owfs_is_dirty(const owfs_superblock_t *sb);
owfs_status_t owfs_sync_changes(htl_device_t *dev, owfs_superblock_t *sb);
owfs_status_t owfs_consistency_check(htl_device_t *dev, owfs_superblock_t *sb, uint32_t *out_corrupt_count);

/* Mount: reads the superblock into `sb`. On a clean volume this increments
 * mount_count, flags the volume DIRTY and returns OWFS_OK. On a volume left
 * dirty/unlocked by a crash it persists the LOCKED state and returns
 * OWFS_ERR_VOLUME_DIRTY, requiring owfs_consistency_check before use. */
owfs_status_t owfs_mount(htl_device_t *dev, owfs_superblock_t *sb);

/* Unmount: clears the DIRTY flag so the volume is left clean. */
owfs_status_t owfs_unmount(htl_device_t *dev, owfs_superblock_t *sb);

#endif /* OWFS_SYNC_H */
