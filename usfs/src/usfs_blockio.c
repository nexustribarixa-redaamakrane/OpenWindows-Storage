#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "../include/usfs_blockio.h"

usfs_status_t usfs_block_read(htl_device_t *dev, uint32_t block, void *buf) {
    if (!dev || !buf) {
        return USFS_ERR_INVALID_PARAM;
    }
    htl_status_t hres = htl_read_block(dev, block, buf);
    if (hres != HTL_OK) {
        return USFS_ERR_IO;
    }
    return USFS_OK;
}

usfs_status_t usfs_block_write(htl_device_t *dev, uint32_t block, const void *buf) {
    if (!dev || !buf) {
        return USFS_ERR_INVALID_PARAM;
    }
    htl_status_t hres = htl_write_block(dev, block, buf);
    if (hres != HTL_OK) {
        if (hres == HTL_ERR_WRITE_PROTECT) {
            return USFS_ERR_WRITE_PROTECTED;
        }
        return USFS_ERR_IO;
    }
    return USFS_OK;
}

usfs_status_t usfs_block_zero(htl_device_t *dev, uint32_t block) {
    if (!dev) {
        return USFS_ERR_INVALID_PARAM;
    }
    htl_status_t hres = htl_zero_block(dev, block);
    if (hres != HTL_OK) {
        return USFS_ERR_IO;
    }
    return USFS_OK;
}
