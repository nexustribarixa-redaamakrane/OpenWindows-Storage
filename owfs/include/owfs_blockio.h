#ifndef OWFS_BLOCKIO_H
#define OWFS_BLOCKIO_H

#include "owfs_types.h"

owfs_status_t owfs_block_read(htl_device_t *dev, uint32_t block, void *buf);
owfs_status_t owfs_block_write(htl_device_t *dev, uint32_t block, const void *buf);
owfs_status_t owfs_block_zero(htl_device_t *dev, uint32_t block);

#endif /* OWFS_BLOCKIO_H */
