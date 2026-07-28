#ifndef OWFS_SYNC_H
#define OWFS_SYNC_H

#include "owfs_types.h"
#include "owfs_superblock.h"

owfs_status_t owfs_mark_dirty(htl_device_t *dev, owfs_superblock_t *sb);
owfs_status_t owfs_mark_clean(htl_device_t *dev, owfs_superblock_t *sb);
bool owfs_is_dirty(const owfs_superblock_t *sb);
owfs_status_t owfs_sync_changes(htl_device_t *dev, owfs_superblock_t *sb);
owfs_status_t owfs_consistency_check(htl_device_t *dev, owfs_superblock_t *sb, uint32_t *out_corrupt_count);

#endif /* OWFS_SYNC_H */
