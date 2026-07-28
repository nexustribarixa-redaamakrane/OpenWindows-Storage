#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "../include/usfs_format.h"
#include "../include/usfs_entry.h"
#include "../include/usfs_sync.h"
#include "../../common/include/ow_mem.h"
#include "../../common/include/ow_string.h"

usfs_status_t usfs_format_volume(htl_device_t *dev, uint32_t total_blocks, const uint8_t *label, size_t label_len) {
    if (!dev || total_blocks < 32) {
        return USFS_ERR_INVALID_PARAM;
    }

    static usfs_superblock_t sb;
    ow_memset(&sb, 0, sizeof(usfs_superblock_t));

    sb.magic = USFS_MAGIC;
    sb.version_major = USFS_VERSION_MAJOR;
    sb.version_minor = USFS_VERSION_MINOR;
    sb.block_size = USFS_BLOCK_SIZE;
    sb.total_blocks = total_blocks;

    /* Entry table starts at block 1 (16 blocks = 256 entries) */
    sb.entry_table_start = 1;
    sb.entry_table_blocks = 16;
    sb.total_entries = sb.entry_table_blocks * (USFS_BLOCK_SIZE / USFS_ENTRY_SIZE);
    sb.used_entries = 0;

    sb.data_region_start = sb.entry_table_start + sb.entry_table_blocks;
    if (total_blocks <= sb.data_region_start) {
        return USFS_ERR_INVALID_PARAM;
    }
    sb.free_blocks = total_blocks - sb.data_region_start;
    sb.security_flags = 0;
    sb.state_flags = USFS_STATE_CLEAN;

    if (label && label_len > 0) {
        ow_sutf8_name_copy(sb.volume_label, sizeof(sb.volume_label), label, label_len);
    }

    /* Zero entry table region */
    static uint8_t zero_block[USFS_BLOCK_SIZE];
    ow_memset(zero_block, 0, sizeof(zero_block));
    for (uint32_t i = 0; i < sb.entry_table_blocks; ++i) {
        htl_status_t hres = htl_write_block(dev, sb.entry_table_start + i, zero_block);
        if (hres != HTL_OK) return USFS_ERR_IO;
    }

    /* Allocate Entry 0 as Root Catalog ('/') */
    usfs_entry_t root_entry;
    ow_memset(&root_entry, 0, sizeof(usfs_entry_t));
    root_entry.entry_index = 0;
    root_entry.entry_type = USFS_ENTRY_CATALOG;
    root_entry.permissions = 075;
    root_entry.parent_entry = 0;
    root_entry.first_block = sb.data_region_start;
    root_entry.block_count = 1;
    root_entry.name_length = 1;
    root_entry.name[0] = '/';

    htl_zero_block(dev, sb.data_region_start);
    sb.free_blocks--;

    usfs_status_t res = usfs_entry_write(dev, &sb, 0, &root_entry);
    if (res != USFS_OK) return res;

    sb.used_entries = 1;
    return usfs_flush_dirty(dev, &sb);
}

usfs_status_t usfs_quick_format(htl_device_t *dev, uint32_t total_blocks, const uint8_t *label, size_t label_len) {
    return usfs_format_volume(dev, total_blocks, label, label_len);
}

usfs_status_t usfs_full_scrub(htl_device_t *dev, uint32_t total_blocks, const uint8_t *label, size_t label_len) {
    if (!dev || total_blocks < 32) {
        return USFS_ERR_INVALID_PARAM;
    }
    for (uint32_t b = 0; b < total_blocks; ++b) {
        htl_status_t hres = htl_zero_block(dev, b);
        if (hres != HTL_OK) return USFS_ERR_IO;
    }
    return usfs_format_volume(dev, total_blocks, label, label_len);
}

usfs_status_t usfs_crypto_purge(htl_device_t *dev, usfs_superblock_t *sb) {
    if (!dev || !sb) {
        return USFS_ERR_INVALID_PARAM;
    }

    /* Pass 1: Overwrite key slots with 0xFF */
    ow_memset(sb->key_slot_1, 0xFF, sizeof(sb->key_slot_1));
    ow_memset(sb->key_slot_2, 0xFF, sizeof(sb->key_slot_2));
    usfs_status_t res = usfs_superblock_write(dev, sb);
    if (res != USFS_OK) return res;
    htl_flush_cache(dev);

    /* Pass 2: Overwrite key slots with 0x00 */
    ow_memset(sb->key_slot_1, 0x00, sizeof(sb->key_slot_1));
    ow_memset(sb->key_slot_2, 0x00, sizeof(sb->key_slot_2));
    res = usfs_superblock_write(dev, sb);
    if (res != USFS_OK) return res;
    htl_flush_cache(dev);

    /* Pass 3: Overwrite key slots with 0xAA */
    ow_memset(sb->key_slot_1, 0xAA, sizeof(sb->key_slot_1));
    ow_memset(sb->key_slot_2, 0xAA, sizeof(sb->key_slot_2));
    res = usfs_superblock_write(dev, sb);
    if (res != USFS_OK) return res;
    htl_flush_cache(dev);

    /* Reset encryption flag */
    sb->security_flags &= ~USFS_SEC_ENCRYPTED;
    return usfs_flush_dirty(dev, sb);
}
