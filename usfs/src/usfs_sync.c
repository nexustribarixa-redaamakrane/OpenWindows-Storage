#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "../include/usfs_sync.h"
#include "../include/usfs_entry.h"

bool usfs_is_dirty(const usfs_superblock_t *sb) {
    if (!sb) {
        return true;
    }
    return (sb->state_flags & (USFS_STATE_DIRTY | USFS_STATE_LOCKED | USFS_STATE_ERROR)) != 0;
}

usfs_status_t usfs_mark_dirty(htl_device_t *dev, usfs_superblock_t *sb) {
    if (!dev || !sb) {
        return USFS_ERR_INVALID_PARAM;
    }
    sb->state_flags |= USFS_STATE_DIRTY;
    return usfs_superblock_write(dev, sb);
}

usfs_status_t usfs_mark_clean(htl_device_t *dev, usfs_superblock_t *sb) {
    if (!dev || !sb) {
        return USFS_ERR_INVALID_PARAM;
    }
    sb->state_flags &= ~(USFS_STATE_DIRTY | USFS_STATE_LOCKED);
    return usfs_superblock_write(dev, sb);
}

usfs_status_t usfs_flush_dirty(htl_device_t *dev, usfs_superblock_t *sb) {
    if (!dev || !sb) {
        return USFS_ERR_INVALID_PARAM;
    }
    sb->state_flags &= ~(USFS_STATE_DIRTY | USFS_STATE_LOCKED);
    usfs_status_t res = usfs_superblock_write(dev, sb);
    if (res != USFS_OK) {
        return res;
    }
    htl_status_t hres = htl_flush_cache(dev);
    if (hres != HTL_OK) {
        return USFS_ERR_IO;
    }
    return USFS_OK;
}

usfs_status_t usfs_consistency_check(htl_device_t *dev, usfs_superblock_t *sb, uint32_t *out_corrupt_count) {
    if (!dev || !sb || !out_corrupt_count) {
        return USFS_ERR_INVALID_PARAM;
    }
    *out_corrupt_count = 0;

    if (!usfs_superblock_validate(sb)) {
        sb->state_flags |= USFS_STATE_ERROR | USFS_STATE_LOCKED;
        (*out_corrupt_count)++;
        return USFS_ERR_CORRUPT_SUPERBLOCK;
    }

    usfs_entry_t entry;
    for (uint32_t i = 0; i < sb->total_entries; ++i) {
        usfs_status_t res = usfs_entry_read(dev, sb, i, &entry);
        if (res == USFS_ERR_CHECKSUM_MISMATCH) {
            (*out_corrupt_count)++;
        }
    }

    if (*out_corrupt_count == 0) {
        sb->state_flags &= ~(USFS_STATE_DIRTY | USFS_STATE_LOCKED | USFS_STATE_ERROR);
        return usfs_superblock_write(dev, sb);
    } else {
        sb->state_flags |= USFS_STATE_ERROR;
        usfs_superblock_write(dev, sb);
        return USFS_ERR_CHECKSUM_MISMATCH;
    }
}
