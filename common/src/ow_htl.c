#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "../include/ow_htl.h"
#include "../include/ow_mem.h"

bool htl_device_valid(const htl_device_t *dev) {
    if (!dev) {
        return false;
    }
    if (!dev->read_block || !dev->write_block) {
        return false;
    }
    if (dev->block_size < 0x200) { /* Minimum 512B block size */
        return false;
    }
    return true;
}

htl_status_t htl_read_block(htl_device_t *dev, uint32_t block_num, void *buf) {
    if (!htl_device_valid(dev) || !buf) {
        return HTL_ERR_INVALID_PARAM;
    }
    if (dev->total_blocks > 0 && block_num >= dev->total_blocks) {
        return HTL_ERR_INVALID_BLOCK;
    }
    return dev->read_block(dev->driver_ctx, block_num, buf, dev->block_size);
}

htl_status_t htl_write_block(htl_device_t *dev, uint32_t block_num, const void *buf) {
    if (!htl_device_valid(dev) || !buf) {
        return HTL_ERR_INVALID_PARAM;
    }
    if (dev->write_protect) {
        return HTL_ERR_WRITE_PROTECT;
    }
    if (dev->total_blocks > 0 && block_num >= dev->total_blocks) {
        return HTL_ERR_INVALID_BLOCK;
    }
    return dev->write_block(dev->driver_ctx, block_num, buf, dev->block_size);
}

htl_status_t htl_flush_cache(htl_device_t *dev) {
    if (!htl_device_valid(dev)) {
        return HTL_ERR_INVALID_PARAM;
    }
    if (dev->flush_cache) {
        return dev->flush_cache(dev->driver_ctx);
    }
    return HTL_OK;
}

htl_status_t htl_zero_block(htl_device_t *dev, uint32_t block_num) {
    if (!htl_device_valid(dev)) {
        return HTL_ERR_INVALID_PARAM;
    }
    static uint8_t zero_buf[4096];
    ow_memset(zero_buf, 0, sizeof(zero_buf));

    uint32_t bs = dev->block_size;
    if (bs <= sizeof(zero_buf)) {
        return htl_write_block(dev, block_num, zero_buf);
    }
    return HTL_ERR_INVALID_PARAM;
}
