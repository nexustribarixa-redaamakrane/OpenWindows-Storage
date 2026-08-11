#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "../include/usfs_sync.h"
#include "../include/usfs_entry.h"
#include "../include/usfs_bitmap.h"

bool usfs_is_dirty(const usfs_superblock_t *sb) {
    if (!sb) {
        return true;
    }
    return (sb->state_flags & (USFS_STATE_DIRTY | USFS_STATE_LOCKED | USFS_STATE_ERROR)) != 0;
}

usfs_status_t usfs_volume_writable(const usfs_superblock_t *sb) {
    if (!sb) {
        return USFS_ERR_VOLUME_DIRTY;
    }
    if (sb->state_flags & (USFS_STATE_LOCKED | USFS_STATE_ERROR)) {
        return USFS_ERR_VOLUME_DIRTY;
    }
    if (sb->security_flags & USFS_SEC_READONLY) {
        return USFS_ERR_WRITE_PROTECTED;
    }
    return USFS_OK;
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
    if (sb->state_flags & USFS_STATE_ERROR) {
        return USFS_ERR_CHECKSUM_MISMATCH;
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

usfs_status_t usfs_flush_dirty(htl_device_t *dev, usfs_superblock_t *sb) {
    if (!dev || !sb) {
        return USFS_ERR_INVALID_PARAM;
    }
    sb->fletcher64_bitmap = usfs_bitmap_compute_fletcher64(dev, sb);
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
        usfs_superblock_write(dev, sb);
        htl_flush_cache(dev);
        return USFS_ERR_CORRUPT_SUPERBLOCK;
    }

    /* Persist the scan lock so a crash mid-scan leaves the volume locked. */
    sb->state_flags |= USFS_STATE_LOCKED;
    usfs_status_t wres = usfs_superblock_write(dev, sb);
    if (wres != USFS_OK) {
        return wres;
    }

    usfs_entry_t entry;
    for (uint32_t i = 0; i < sb->total_entries; ++i) {
        usfs_status_t res = usfs_entry_read(dev, sb, i, &entry);
        if (res == USFS_ERR_CHECKSUM_MISMATCH) {
            (*out_corrupt_count)++;
        } else if (res != USFS_OK && res != USFS_ERR_NOT_FOUND) {
            return res;
        }
    }

    uint64_t current_fletcher = usfs_bitmap_compute_fletcher64(dev, sb);
    if (sb->fletcher64_bitmap != 0 && current_fletcher != sb->fletcher64_bitmap) {
        (*out_corrupt_count)++;
    }

    uint32_t counted_free = usfs_bitmap_count_free(dev, sb);
    if (counted_free != sb->free_blocks) {
        (*out_corrupt_count)++;
    }

    if ((sb->security_flags & USFS_SEC_SIGNED) && sb->signature != usfs_signature_compute(dev, sb)) {
        (*out_corrupt_count)++;
    }

    if (*out_corrupt_count == 0) {
        sb->state_flags &= ~(USFS_STATE_DIRTY | USFS_STATE_LOCKED | USFS_STATE_ERROR);
        wres = usfs_superblock_write(dev, sb);
        if (wres != USFS_OK) {
            return wres;
        }
        htl_flush_cache(dev);
        return USFS_OK;
    } else {
        sb->state_flags |= USFS_STATE_ERROR;
        usfs_superblock_write(dev, sb);
        htl_flush_cache(dev);
        return USFS_ERR_CHECKSUM_MISMATCH;
    }
}

usfs_status_t usfs_mount(htl_device_t *dev, usfs_superblock_t *sb) {
    if (!dev || !sb) {
        return USFS_ERR_INVALID_PARAM;
    }
    usfs_status_t res = usfs_superblock_read(dev, sb);
    if (res != USFS_OK) {
        return res;
    }
    if (sb->state_flags & USFS_STATE_ERROR) {
        return USFS_ERR_VOLUME_DIRTY;
    }
    if (sb->state_flags & (USFS_STATE_DIRTY | USFS_STATE_LOCKED)) {
        sb->state_flags |= USFS_STATE_LOCKED;
        res = usfs_superblock_write(dev, sb);
        if (res != USFS_OK) {
            return res;
        }
        return USFS_ERR_VOLUME_DIRTY;
    }
    sb->mount_count++;
    sb->state_flags |= USFS_STATE_DIRTY;
    return usfs_superblock_write(dev, sb);
}

usfs_status_t usfs_unmount(htl_device_t *dev, usfs_superblock_t *sb) {
    if (!dev || !sb) {
        return USFS_ERR_INVALID_PARAM;
    }
    if (sb->state_flags & USFS_STATE_ERROR) {
        return USFS_ERR_CHECKSUM_MISMATCH;
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
