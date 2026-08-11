#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "../include/usfs_format.h"
#include "../include/usfs_entry.h"
#include "../include/usfs_bitmap.h"
#include "../include/usfs_sync.h"
#include "../../common/include/ow_mem.h"
#include "../../common/include/ow_string.h"

static uint32_t layout_bitmap_blocks(uint32_t total_blocks, uint32_t *out_data_region_start) {
    uint32_t b = 1;
    for (;;) {
        if (total_blocks <= 17 + b) {
            return 0;
        }
        uint32_t data = total_blocks - 17 - b;
        uint32_t needed = (data + (USFS_BLOCK_SIZE * 8) - 1) / (USFS_BLOCK_SIZE * 8);
        if (needed > b) {
            b = needed;
            continue;
        }
        if (out_data_region_start) {
            *out_data_region_start = 17 + b;
        }
        return b;
    }
}

usfs_status_t usfs_format_volume(htl_device_t *dev, uint32_t total_blocks, const uint8_t *label, size_t label_len) {
    if (!dev || total_blocks < 19) {
        return USFS_ERR_INVALID_PARAM;
    }

    uint32_t data_region_start = 0;
    uint32_t bitmap_blocks = layout_bitmap_blocks(total_blocks, &data_region_start);
    if (bitmap_blocks == 0) {
        return USFS_ERR_INVALID_PARAM;
    }

    static usfs_superblock_t sb;
    ow_memset(&sb, 0, sizeof(usfs_superblock_t));

    sb.magic = USFS_MAGIC;
    sb.version_major = USFS_VERSION_MAJOR;
    sb.version_minor = USFS_VERSION_MINOR;
    sb.block_size = USFS_BLOCK_SIZE;
    sb.total_blocks = total_blocks;
    sb.entry_table_start = 1;
    sb.entry_table_blocks = USFS_ENTRY_TABLE_BLOCKS;
    sb.total_entries = sb.entry_table_blocks * USFS_ENTRIES_PER_BLOCK;
    sb.used_entries = 0;
    sb.bitmap_start_block = 17;
    sb.bitmap_block_count = bitmap_blocks;
    sb.data_region_start = data_region_start;
    sb.free_blocks = total_blocks - data_region_start;
    sb.security_flags = 0;
    sb.state_flags = USFS_STATE_CLEAN;
    sb.mount_count = 0;
    sb.signature = 0;
    sb.fletcher64_bitmap = 0;

    if (label && label_len > 0) {
        ow_sutf8_name_copy(sb.volume_label, sizeof(sb.volume_label), label, label_len);
    }

    static uint8_t zero_block[USFS_BLOCK_SIZE];
    ow_memset(zero_block, 0, sizeof(zero_block));

    for (uint32_t i = 0; i < sb.entry_table_blocks; ++i) {
        if (htl_write_block(dev, sb.entry_table_start + i, zero_block) != HTL_OK) {
            return USFS_ERR_IO;
        }
    }

    usfs_status_t res = usfs_bitmap_init(dev, &sb);
    if (res != USFS_OK) {
        return res;
    }

    static const uint8_t root_name[] = "/";
    uint32_t root_idx = 0;
    res = usfs_entry_alloc(dev, &sb, USFS_ENTRY_CATALOG, root_name, 1, 0, &root_idx);
    if (res != USFS_OK) {
        return res;
    }

    sb.used_entries = 1;
    sb.fletcher64_bitmap = usfs_bitmap_compute_fletcher64(dev, &sb);
    sb.signature = usfs_signature_compute(dev, &sb);
    return usfs_flush_dirty(dev, &sb);
}

usfs_status_t usfs_quick_format(htl_device_t *dev, uint32_t total_blocks, const uint8_t *label, size_t label_len) {
    return usfs_format_volume(dev, total_blocks, label, label_len);
}

usfs_status_t usfs_full_scrub(htl_device_t *dev, uint32_t total_blocks, const uint8_t *label, size_t label_len) {
    if (!dev || total_blocks < 19) {
        return USFS_ERR_INVALID_PARAM;
    }
    for (uint32_t b = 0; b < total_blocks; ++b) {
        htl_status_t hres = htl_zero_block(dev, b);
        if (hres != HTL_OK) {
            if (hres == HTL_ERR_WRITE_PROTECT) {
                return USFS_ERR_WRITE_PROTECTED;
            }
            return USFS_ERR_IO;
        }
    }
    return usfs_format_volume(dev, total_blocks, label, label_len);
}

usfs_status_t usfs_crypto_set_key(htl_device_t *dev, usfs_superblock_t *sb, const uint8_t *key, size_t key_len) {
    if (!dev || !sb || !key) {
        return USFS_ERR_INVALID_PARAM;
    }
    if (key_len != USFS_KEY_SIZE) {
        return USFS_ERR_KEY_INVALID;
    }
    if (usfs_volume_writable(sb) != USFS_OK) {
        return USFS_ERR_VOLUME_DIRTY;
    }

    ow_memset(sb->key_slot_1, 0, sizeof(sb->key_slot_1));
    ow_memcpy(sb->key_slot_1, key, USFS_KEY_SIZE);
    sb->security_flags |= USFS_SEC_ENCRYPTED;
    usfs_status_t res = usfs_superblock_write(dev, sb);
    if (res != USFS_OK) {
        return res;
    }
    htl_flush_cache(dev);
    return USFS_OK;
}

usfs_status_t usfs_crypto_purge(htl_device_t *dev, usfs_superblock_t *sb) {
    if (!dev || !sb) {
        return USFS_ERR_INVALID_PARAM;
    }

    ow_memset(sb->key_slot_1, 0xFF, sizeof(sb->key_slot_1));
    ow_memset(sb->key_slot_2, 0xFF, sizeof(sb->key_slot_2));
    usfs_status_t res = usfs_superblock_write(dev, sb);
    if (res != USFS_OK) return res;
    htl_flush_cache(dev);

    ow_memset(sb->key_slot_1, 0x00, sizeof(sb->key_slot_1));
    ow_memset(sb->key_slot_2, 0x00, sizeof(sb->key_slot_2));
    res = usfs_superblock_write(dev, sb);
    if (res != USFS_OK) return res;
    htl_flush_cache(dev);

    ow_memset(sb->key_slot_1, 0xAA, sizeof(sb->key_slot_1));
    ow_memset(sb->key_slot_2, 0xAA, sizeof(sb->key_slot_2));
    res = usfs_superblock_write(dev, sb);
    if (res != USFS_OK) return res;
    htl_flush_cache(dev);

    sb->security_flags &= ~USFS_SEC_ENCRYPTED;
    return usfs_flush_dirty(dev, sb);
}
