#ifndef OWFS_BITMAP_H
#define OWFS_BITMAP_H

#include "owfs_types.h"
#include "owfs_superblock.h"

owfs_status_t owfs_bitmap_alloc(htl_device_t *dev, owfs_superblock_t *sb, uint32_t *out_block);
owfs_status_t owfs_bitmap_free(htl_device_t *dev, owfs_superblock_t *sb, uint32_t block);
bool owfs_bitmap_is_used(htl_device_t *dev, const owfs_superblock_t *sb, uint32_t block);
owfs_status_t owfs_bitmap_init(htl_device_t *dev, const owfs_superblock_t *sb);
uint64_t owfs_bitmap_compute_fletcher64(htl_device_t *dev, const owfs_superblock_t *sb);

#endif /* OWFS_BITMAP_H */
