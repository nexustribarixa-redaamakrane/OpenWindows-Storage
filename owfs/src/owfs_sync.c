#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "../include/owfs_sync.h"
#include "../include/owfs_superblock.h"
#include "../include/owfs_inode.h"
#include "../include/owfs_bitmap.h"

bool owfs_is_dirty(const owfs_superblock_t *sb) {
    if (!sb) {
        return true;
    }
    return (sb->state_flags & (OWFS_STATE_DIRTY | OWFS_STATE_LOCKED | OWFS_STATE_ERROR)) != 0;
}

owfs_status_t owfs_volume_writable(const owfs_superblock_t *sb) {
    if (!sb) {
        return OWFS_ERR_VOLUME_DIRTY;
    }
    if (sb->state_flags & (OWFS_STATE_LOCKED | OWFS_STATE_ERROR)) {
        return OWFS_ERR_VOLUME_DIRTY;
    }
    if (sb->security_flags & OWFS_SEC_READONLY) {
        return OWFS_ERR_WRITE_PROTECTED;
    }
    return OWFS_OK;
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
    if (sb->state_flags & OWFS_STATE_ERROR) {
        return OWFS_ERR_CHECKSUM_MISMATCH;
    }
    sb->state_flags &= ~(OWFS_STATE_DIRTY | OWFS_STATE_LOCKED);
    owfs_status_t res = owfs_superblock_write(dev, sb);
    if (res != OWFS_OK) {
        return res;
    }
    htl_status_t hres = htl_flush_cache(dev);
    if (hres != HTL_OK) {
        return OWFS_ERR_IO;
    }
    return OWFS_OK;
}

owfs_status_t owfs_sync_changes(htl_device_t *dev, owfs_superblock_t *sb) {
    if (!dev || !sb) {
        return OWFS_ERR_INVALID_PARAM;
    }

    sb->fletcher64_bitmap = owfs_bitmap_compute_fletcher64(dev, sb);

    sb->state_flags &= ~(OWFS_STATE_DIRTY | OWFS_STATE_LOCKED);
    owfs_status_t res = owfs_superblock_write(dev, sb);
    if (res != OWFS_OK) {
        return res;
    }

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

    if (!owfs_superblock_validate(sb)) {
        sb->state_flags |= OWFS_STATE_ERROR | OWFS_STATE_LOCKED;
        (*out_corrupt_count)++;
        owfs_superblock_write(dev, sb);
        htl_flush_cache(dev);
        return OWFS_ERR_CORRUPT_SUPERBLOCK;
    }

    /* Persist the scan lock so a crash mid-scan leaves the volume locked. */
    sb->state_flags |= OWFS_STATE_LOCKED;
    owfs_status_t wres = owfs_superblock_write(dev, sb);
    if (wres != OWFS_OK) {
        return wres;
    }

    owfs_inode_t inode;
    for (uint32_t i = 0; i < sb->total_inodes; ++i) {
        owfs_status_t res = owfs_inode_read(dev, sb, i, &inode);
        if (res == OWFS_ERR_CHECKSUM_MISMATCH) {
            (*out_corrupt_count)++;
        } else if (res != OWFS_OK && res != OWFS_ERR_NOT_FOUND) {
            return res;
        }
    }

    uint64_t current_fletcher = owfs_bitmap_compute_fletcher64(dev, sb);
    if (sb->fletcher64_bitmap != 0 && current_fletcher != sb->fletcher64_bitmap) {
        (*out_corrupt_count)++;
    }

    uint32_t counted_free = owfs_bitmap_count_free(dev, sb);
    if (counted_free != sb->free_blocks) {
        (*out_corrupt_count)++;
    }

    if (*out_corrupt_count == 0) {
        sb->state_flags &= ~(OWFS_STATE_DIRTY | OWFS_STATE_LOCKED | OWFS_STATE_ERROR);
        wres = owfs_superblock_write(dev, sb);
        if (wres != OWFS_OK) {
            return wres;
        }
        htl_flush_cache(dev);
        return OWFS_OK;
    } else {
        sb->state_flags |= OWFS_STATE_ERROR;
        owfs_superblock_write(dev, sb);
        htl_flush_cache(dev);
        return OWFS_ERR_CHECKSUM_MISMATCH;
    }
}

owfs_status_t owfs_mount(htl_device_t *dev, owfs_superblock_t *sb) {
    if (!dev || !sb) {
        return OWFS_ERR_INVALID_PARAM;
    }
    owfs_status_t res = owfs_superblock_read(dev, sb);
    if (res != OWFS_OK) {
        return res;
    }
    if (sb->state_flags & OWFS_STATE_ERROR) {
        return OWFS_ERR_VOLUME_DIRTY;
    }
    if (sb->state_flags & (OWFS_STATE_DIRTY | OWFS_STATE_LOCKED)) {
        sb->state_flags |= OWFS_STATE_LOCKED;
        res = owfs_superblock_write(dev, sb);
        if (res != OWFS_OK) {
            return res;
        }
        return OWFS_ERR_VOLUME_DIRTY;
    }
    sb->mount_count++;
    sb->state_flags |= OWFS_STATE_DIRTY;
    return owfs_superblock_write(dev, sb);
}

owfs_status_t owfs_unmount(htl_device_t *dev, owfs_superblock_t *sb) {
    if (!dev || !sb) {
        return OWFS_ERR_INVALID_PARAM;
    }
    if (sb->state_flags & OWFS_STATE_ERROR) {
        return OWFS_ERR_CHECKSUM_MISMATCH;
    }
    sb->state_flags &= ~(OWFS_STATE_DIRTY | OWFS_STATE_LOCKED);
    owfs_status_t res = owfs_superblock_write(dev, sb);
    if (res != OWFS_OK) {
        return res;
    }
    htl_status_t hres = htl_flush_cache(dev);
    if (hres != HTL_OK) {
        return OWFS_ERR_IO;
    }
    return OWFS_OK;
}
