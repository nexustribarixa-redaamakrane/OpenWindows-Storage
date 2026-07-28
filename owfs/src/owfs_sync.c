#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "../include/owfs_sync.h"
#include "../include/owfs_inode.h"
#include "../include/owfs_bitmap.h"

bool owfs_is_dirty(const owfs_superblock_t *sb) {
    if (!sb) {
        return true;
    }
    return (sb->state_flags & (OWFS_STATE_DIRTY | OWFS_STATE_LOCKED | OWFS_STATE_ERROR)) != 0;
}

owfs_status_t owfs_mark_dirty(htl_device_t *dev, owfs_superblock_t *sb) {
    if (!dev || !sb) {
        return OWFS_ERR_INVALID_PARAM;
    }
    sb->state_flags |= OWFS_STATE_DIRTY;
    return owfs_superblock_write(dev, sb);
}

owfs_status_t owfs_mark_clean(htl_device_t *dev, owfs_superblock_t *sb) {
    if (!dev || !sb) {
        return OWFS_ERR_INVALID_PARAM;
    }
    sb->state_flags &= ~(OWFS_STATE_DIRTY | OWFS_STATE_LOCKED);
    return owfs_superblock_write(dev, sb);
}

owfs_status_t owfs_sync_changes(htl_device_t *dev, owfs_superblock_t *sb) {
    if (!dev || !sb) {
        return OWFS_ERR_INVALID_PARAM;
    }

    /* 1. Recompute Fletcher-64 checksum of block allocation bitmap */
    sb->fletcher64_bitmap = owfs_bitmap_compute_fletcher64(dev, sb);

    /* 2. Recompute CRC32c and write superblock */
    sb->state_flags &= ~(OWFS_STATE_DIRTY | OWFS_STATE_LOCKED);
    owfs_status_t res = owfs_superblock_write(dev, sb);
    if (res != OWFS_OK) {
        return res;
    }

    /* 3. Execute bare-metal physical storage cache flush */
    htl_status_t hres = htl_flush_cache(dev);
    if (hres != HTL_OK) {
        return OWFS_ERR_IO;
    }
    return OWFS_OK;
}

owfs_status_t owfs_consistency_check(htl_device_t *dev, owfs_superblock_t *sb, uint32_t *out_corrupt_count) {
    if (!dev || !sb || !out_corrupt_count) {
        return OWFS_ERR_INVALID_PARAM;
    }
    *out_corrupt_count = 0;

    /* 1. Verify Superblock */
    if (!owfs_superblock_validate(sb)) {
        sb->state_flags |= OWFS_STATE_ERROR | OWFS_STATE_LOCKED;
        (*out_corrupt_count)++;
        return OWFS_ERR_CORRUPT_SUPERBLOCK;
    }

    /* 2. Scan Inode Table for CRC32c corruptions */
    owfs_inode_t inode;
    for (uint32_t i = 0; i < sb->total_inodes; ++i) {
        owfs_status_t res = owfs_inode_read(dev, sb, i, &inode);
        if (res == OWFS_ERR_CHECKSUM_MISMATCH) {
            (*out_corrupt_count)++;
        }
    }

    /* 3. Check bitmap Fletcher-64 */
    uint64_t current_fletcher = owfs_bitmap_compute_fletcher64(dev, sb);
    if (sb->fletcher64_bitmap != 0 && current_fletcher != sb->fletcher64_bitmap) {
        (*out_corrupt_count)++;
    }

    if (*out_corrupt_count == 0) {
        sb->state_flags &= ~(OWFS_STATE_DIRTY | OWFS_STATE_LOCKED | OWFS_STATE_ERROR);
        return owfs_superblock_write(dev, sb);
    } else {
        sb->state_flags |= OWFS_STATE_ERROR;
        owfs_superblock_write(dev, sb);
        return OWFS_ERR_CHECKSUM_MISMATCH;
    }
}
