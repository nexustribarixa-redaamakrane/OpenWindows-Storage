#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "../include/owfs_superblock.h"
#include "../../common/include/ow_checksum.h"

uint32_t owfs_superblock_compute_checksum(const owfs_superblock_t *sb) {
    if (!sb) {
        return 0;
    }
    return ow_crc32c_struct(sb, sizeof(owfs_superblock_t), offsetof(owfs_superblock_t, checksum));
}

bool owfs_superblock_validate(const owfs_superblock_t *sb) {
    if (!sb) {
        return false;
    }
    if (sb->magic != OWFS_MAGIC) {
        return false;
    }
    if (sb->block_size != OWFS_BLOCK_SIZE) {
        return false;
    }
    uint32_t expected_crc = owfs_superblock_compute_checksum(sb);
    if (sb->checksum != expected_crc) {
        return false;
    }
    return true;
}

owfs_status_t owfs_superblock_read(htl_device_t *dev, owfs_superblock_t *sb) {
    if (!dev || !sb) {
        return OWFS_ERR_INVALID_PARAM;
    }
    htl_status_t hres = htl_read_block(dev, 0, sb);
    if (hres != HTL_OK) {
        return OWFS_ERR_IO;
    }
    if (!owfs_superblock_validate(sb)) {
        return OWFS_ERR_CORRUPT_SUPERBLOCK;
    }
    return OWFS_OK;
}

owfs_status_t owfs_superblock_write(htl_device_t *dev, owfs_superblock_t *sb) {
    if (!dev || !sb) {
        return OWFS_ERR_INVALID_PARAM;
    }
    sb->checksum = owfs_superblock_compute_checksum(sb);
    htl_status_t hres = htl_write_block(dev, 0, sb);
    if (hres != HTL_OK) {
        if (hres == HTL_ERR_WRITE_PROTECT) {
            return OWFS_ERR_WRITE_PROTECTED;
        }
        return OWFS_ERR_IO;
    }
    return OWFS_OK;
}
