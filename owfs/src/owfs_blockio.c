#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "../include/owfs_blockio.h"

owfs_status_t owfs_block_read(htl_device_t *dev, uint32_t block, void *buf) {
    if (!dev || !buf) {
        return OWFS_ERR_INVALID_PARAM;
    }
    htl_status_t hres = htl_read_block(dev, block, buf);
    if (hres != HTL_OK) {
        return OWFS_ERR_IO;
    }
    return OWFS_OK;
}

owfs_status_t owfs_block_write(htl_device_t *dev, uint32_t block, const void *buf) {
    if (!dev || !buf) {
        return OWFS_ERR_INVALID_PARAM;
    }
    htl_status_t hres = htl_write_block(dev, block, buf);
    if (hres != HTL_OK) {
        if (hres == HTL_ERR_WRITE_PROTECT) {
            return OWFS_ERR_WRITE_PROTECTED;
        }
        return OWFS_ERR_IO;
    }
    return OWFS_OK;
}

owfs_status_t owfs_block_zero(htl_device_t *dev, uint32_t block) {
    if (!dev) {
        return OWFS_ERR_INVALID_PARAM;
    }
    htl_status_t hres = htl_zero_block(dev, block);
    if (hres != HTL_OK) {
        if (hres == HTL_ERR_WRITE_PROTECT) {
            return OWFS_ERR_WRITE_PROTECTED;
        }
        return OWFS_ERR_IO;
    }
    return OWFS_OK;
}
