#ifndef USFS_SYNC_H
#define USFS_SYNC_H

#include "usfs_types.h"
#include "usfs_superblock.h"

usfs_status_t usfs_mark_dirty(htl_device_t *dev, usfs_superblock_t *sb);
usfs_status_t usfs_mark_clean(htl_device_t *dev, usfs_superblock_t *sb);
bool usfs_is_dirty(const usfs_superblock_t *sb);
usfs_status_t usfs_flush_dirty(htl_device_t *dev, usfs_superblock_t *sb);
usfs_status_t usfs_consistency_check(htl_device_t *dev, usfs_superblock_t *sb, uint32_t *out_corrupt_count);

#endif /* USFS_SYNC_H */
