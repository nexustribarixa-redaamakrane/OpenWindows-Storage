#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "../include/usfs_superblock.h"
#include "../../common/include/ow_checksum.h"

uint32_t usfs_superblock_compute_checksum(const usfs_superblock_t *sb) {
    if (!sb) {
        return 0;
    }
    return ow_crc32c_struct(sb, sizeof(usfs_superblock_t), offsetof(usfs_superblock_t, checksum));
}

bool usfs_superblock_validate(const usfs_superblock_t *sb) {
    if (!sb) {
        return false;
    }
    if (sb->magic != USFS_MAGIC) {
        return false;
    }
    if (sb->block_size != USFS_BLOCK_SIZE) {
        return false;
    }
    uint32_t expected_crc = usfs_superblock_compute_checksum(sb);
    if (sb->checksum != expected_crc) {
        return false;
    }
    return true;
}

usfs_status_t usfs_superblock_read(htl_device_t *dev, usfs_superblock_t *sb) {
    if (!dev || !sb) {
        return USFS_ERR_INVALID_PARAM;
    }
    htl_status_t hres = htl_read_block(dev, 0, sb);
    if (hres != HTL_OK) {
        return USFS_ERR_IO;
    }
    if (!usfs_superblock_validate(sb)) {
        return USFS_ERR_CORRUPT_SUPERBLOCK;
    }
    return USFS_OK;
}

usfs_status_t usfs_superblock_write(htl_device_t *dev, usfs_superblock_t *sb) {
    if (!dev || !sb) {
        return USFS_ERR_INVALID_PARAM;
    }
    sb->checksum = usfs_superblock_compute_checksum(sb);
    htl_status_t hres = htl_write_block(dev, 0, sb);
    if (hres != HTL_OK) {
        if (hres == HTL_ERR_WRITE_PROTECT) {
            return USFS_ERR_WRITE_PROTECTED;
        }
        return USFS_ERR_IO;
    }
    return USFS_OK;
}

uint32_t usfs_signature_compute(htl_device_t *dev, const usfs_superblock_t *sb) {
    if (!dev || !sb) {
        return 0;
    }
    static uint8_t buf[USFS_BLOCK_SIZE];
    uint32_t crc = 0;
    for (uint32_t i = 0; i < sb->entry_table_blocks; ++i) {
        htl_status_t hres = htl_read_block(dev, sb->entry_table_start + i, buf);
        if (hres != HTL_OK) {
            return 0;
        }
        crc = ow_crc32c(crc, buf, USFS_BLOCK_SIZE);
    }
    return crc;
}

usfs_status_t usfs_signature_update(htl_device_t *dev, usfs_superblock_t *sb) {
    if (!dev || !sb) {
        return USFS_ERR_INVALID_PARAM;
    }
    sb->signature = usfs_signature_compute(dev, sb);
    return usfs_superblock_write(dev, sb);
}
