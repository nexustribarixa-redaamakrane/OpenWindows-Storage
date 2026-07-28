#ifndef USFS_BLOCKIO_H
#define USFS_BLOCKIO_H

#include "usfs_types.h"

usfs_status_t usfs_block_read(htl_device_t *dev, uint32_t block, void *buf);
usfs_status_t usfs_block_write(htl_device_t *dev, uint32_t block, const void *buf);
usfs_status_t usfs_block_zero(htl_device_t *dev, uint32_t block);

#endif /* USFS_BLOCKIO_H */
