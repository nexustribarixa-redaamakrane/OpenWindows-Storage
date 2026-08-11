#ifndef OWFS_FORMAT_H
#define OWFS_FORMAT_H

#include "owfs_types.h"
#include "owfs_superblock.h"

owfs_status_t owfs_format_volume(htl_device_t *dev, uint32_t total_blocks, const uint8_t *label, size_t label_len);
owfs_status_t owfs_quick_format(htl_device_t *dev, uint32_t total_blocks, const uint8_t *label, size_t label_len);
owfs_status_t owfs_full_scrub(htl_device_t *dev, uint32_t total_blocks, const uint8_t *label, size_t label_len);

/* Install a 32-byte ChaCha20 key into key_slot_1 and enable volume encryption. */
owfs_status_t owfs_crypto_set_key(htl_device_t *dev, owfs_superblock_t *sb, const uint8_t *key, size_t key_len);

/* Cryptographically sanitize both key slots (3-pass overwrite with cache
 * flushes) and disable volume encryption. */
owfs_status_t owfs_crypto_purge(htl_device_t *dev, owfs_superblock_t *sb);

#endif /* OWFS_FORMAT_H */
