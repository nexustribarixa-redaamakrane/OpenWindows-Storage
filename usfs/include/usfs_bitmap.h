#ifndef USFS_BITMAP_H
#define USFS_BITMAP_H

#include "usfs_types.h"
#include "usfs_superblock.h"

usfs_status_t usfs_bitmap_init(htl_device_t *dev, const usfs_superblock_t *sb);

/* Allocate a contiguous run of `count` free data blocks. */
usfs_status_t usfs_block_alloc(htl_device_t *dev, usfs_superblock_t *sb, uint32_t count, uint32_t *out_first);

usfs_status_t usfs_block_free(htl_device_t *dev, usfs_superblock_t *sb, uint32_t first, uint32_t count);
bool usfs_block_is_used(htl_device_t *dev, const usfs_superblock_t *sb, uint32_t block);
uint64_t usfs_bitmap_compute_fletcher64(htl_device_t *dev, const usfs_superblock_t *sb);
uint32_t usfs_bitmap_count_free(htl_device_t *dev, const usfs_superblock_t *sb);

#endif /* USFS_BITMAP_H */
