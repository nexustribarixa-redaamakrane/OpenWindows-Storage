#ifndef OWFS_FORMAT_H
#define OWFS_FORMAT_H

#include "owfs_types.h"
#include "owfs_superblock.h"

owfs_status_t owfs_format_volume(htl_device_t *dev, uint32_t total_blocks, const uint8_t *label, size_t label_len);
owfs_status_t owfs_quick_format(htl_device_t *dev, uint32_t total_blocks, const uint8_t *label, size_t label_len);
owfs_status_t owfs_full_scrub(htl_device_t *dev, uint32_t total_blocks, const uint8_t *label, size_t label_len);

#endif /* OWFS_FORMAT_H */
