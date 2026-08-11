#ifndef USFS_FORMAT_H
#define USFS_FORMAT_H

#include "usfs_types.h"
#include "usfs_superblock.h"

usfs_status_t usfs_format_volume(htl_device_t *dev, uint32_t total_blocks, const uint8_t *label, size_t label_len);
usfs_status_t usfs_quick_format(htl_device_t *dev, uint32_t total_blocks, const uint8_t *label, size_t label_len);
usfs_status_t usfs_full_scrub(htl_device_t *dev, uint32_t total_blocks, const uint8_t *label, size_t label_len);
usfs_status_t usfs_crypto_purge(htl_device_t *dev, usfs_superblock_t *sb);

/* Install a 32-byte ChaCha20 key into key_slot_1 and enable volume encryption. */
usfs_status_t usfs_crypto_set_key(htl_device_t *dev, usfs_superblock_t *sb, const uint8_t *key, size_t key_len);

#endif /* USFS_FORMAT_H */
