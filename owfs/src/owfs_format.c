#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "../include/owfs_format.h"
#include "../include/owfs_inode.h"
#include "../include/owfs_bitmap.h"
#include "../include/owfs_sync.h"
#include "../../common/include/ow_mem.h"
#include "../../common/include/ow_string.h"

static owfs_status_t map_htl(htl_status_t hres) {
    if (hres == HTL_ERR_WRITE_PROTECT) {
        return OWFS_ERR_WRITE_PROTECTED;
    }
    return OWFS_ERR_IO;
}

static owfs_status_t zero_block_checked(htl_device_t *dev, uint32_t block) {
    htl_status_t hres = htl_zero_block(dev, block);
    if (hres != HTL_OK) {
        return map_htl(hres);
    }
    return OWFS_OK;
}

owfs_status_t owfs_format_volume(htl_device_t *dev, uint32_t total_blocks, const uint8_t *label, size_t label_len) {
    if (!dev) {
        return OWFS_ERR_INVALID_PARAM;
    }
    if (dev->block_size != OWFS_BLOCK_SIZE) {
        return OWFS_ERR_INVALID_PARAM;
    }
    if (dev->total_blocks > 0 && total_blocks > dev->total_blocks) {
        return OWFS_ERR_INVALID_PARAM;
    }
    if (total_blocks < (OWFS_SUPERBLOCK_BLOCK + 1 + 1 + OWFS_INODE_TABLE_BLOCKS + 1)) {
        return OWFS_ERR_INVALID_PARAM;
    }

    /* Superblock lives at OWFS_SUPERBLOCK_BLOCK (block 16); blocks 0-15 are
     * reserved for the Modular Bootloader (MBL). The bitmap must cover the
     * data region: choose the smallest bitmap_blocks such that
     *   data_blocks = total_blocks - (17 + bitmap_blocks + inode_table_blocks)
     * is positive and no larger than bitmap_blocks * (4096 * 8). */
    uint32_t fixed_overhead = OWFS_SUPERBLOCK_BLOCK + 1 + OWFS_INODE_TABLE_BLOCKS; /* 273 */
    uint32_t bitmap_blocks = (total_blocks - fixed_overhead + 32768) / 32769;

    static owfs_superblock_t sb;
    ow_memset(&sb, 0, sizeof(owfs_superblock_t));

    sb.magic = OWFS_MAGIC;
    sb.version_major = OWFS_VERSION_MAJOR;
    sb.version_minor = OWFS_VERSION_MINOR;
    sb.block_size = OWFS_BLOCK_SIZE;
    sb.total_blocks = total_blocks;

    sb.bitmap_start_block = OWFS_SUPERBLOCK_BLOCK + 1;
    sb.bitmap_block_count = bitmap_blocks;

    sb.inode_table_start = sb.bitmap_start_block + sb.bitmap_block_count;
    sb.inode_table_blocks = OWFS_INODE_TABLE_BLOCKS;
    sb.total_inodes = OWFS_TOTAL_INODES;
    sb.free_inodes = sb.total_inodes;

    sb.data_region_start = sb.inode_table_start + sb.inode_table_blocks;
    if (total_blocks <= sb.data_region_start) {
        return OWFS_ERR_INVALID_PARAM;
    }
    sb.free_blocks = total_blocks - sb.data_region_start;
    sb.root_inode = OWFS_ROOT_INODE;
    sb.mount_count = 0;
    sb.state_flags = OWFS_STATE_CLEAN;

    if (label && label_len > 0) {
        ow_sutf8_name_copy(sb.volume_label, sizeof(sb.volume_label), label, label_len);
    }

    /* Draw a fresh per-volume ChaCha20 nonce so that re-formatting with a
     * reused key still produces a distinct keystream. Falls back to zeros
     * when the device provides no entropy source. */
    if (htl_get_entropy(dev, sb.crypto_nonce, sizeof(sb.crypto_nonce)) != HTL_OK) {
        ow_memset(sb.crypto_nonce, 0, sizeof(sb.crypto_nonce));
    }

    owfs_status_t res = owfs_bitmap_init(dev, &sb);
    if (res != OWFS_OK) {
        return res;
    }

    static uint8_t zero_block[OWFS_BLOCK_SIZE];
    ow_memset(zero_block, 0, sizeof(zero_block));
    for (uint32_t i = 0; i < sb.inode_table_blocks; ++i) {
        uint32_t bnum = sb.inode_table_start + i;
        htl_status_t hres = htl_write_block(dev, bnum, zero_block);
        if (hres != HTL_OK) {
            return map_htl(hres);
        }
    }

    owfs_inode_t root_ino;
    ow_memset(&root_ino, 0, sizeof(owfs_inode_t));
    root_ino.inode_number = OWFS_ROOT_INODE;
    root_ino.entry_type = OWFS_ENTRY_CATALOG;
    root_ino.permissions = OWFS_MODE_DEFAULT_DIR;
    root_ino.parent_inode = OWFS_ROOT_INODE;
    root_ino.name_length = 1;
    root_ino.name[0] = '/';

    uint32_t root_block = 0;
    res = owfs_bitmap_alloc(dev, &sb, &root_block);
    if (res != OWFS_OK) {
        return res;
    }
    res = zero_block_checked(dev, root_block);
    if (res != OWFS_OK) {
        owfs_bitmap_free(dev, &sb, root_block);
        return res;
    }
    root_ino.direct_blocks[0] = root_block;
    root_ino.block_count = 1;

    res = owfs_inode_write(dev, &sb, OWFS_ROOT_INODE, &root_ino);
    if (res != OWFS_OK) {
        return res;
    }

    return owfs_sync_changes(dev, &sb);
}

owfs_status_t owfs_quick_format(htl_device_t *dev, uint32_t total_blocks, const uint8_t *label, size_t label_len) {
    /* Re-initialize superblock, bitmap, inode table and root catalog while
     * preserving existing data blocks. */
    return owfs_format_volume(dev, total_blocks, label, label_len);
}

owfs_status_t owfs_full_scrub(htl_device_t *dev, uint32_t total_blocks, const uint8_t *label, size_t label_len) {
    if (!dev) {
        return OWFS_ERR_INVALID_PARAM;
    }
    if (dev->block_size != OWFS_BLOCK_SIZE) {
        return OWFS_ERR_INVALID_PARAM;
    }
    for (uint32_t b = 0; b < total_blocks; ++b) {
        htl_status_t hres = htl_zero_block(dev, b);
        if (hres != HTL_OK) {
            return map_htl(hres);
        }
    }
    return owfs_format_volume(dev, total_blocks, label, label_len);
}

owfs_status_t owfs_crypto_set_key(htl_device_t *dev, owfs_superblock_t *sb, const uint8_t *key, size_t key_len) {
    if (!dev || !sb || !key) {
        return OWFS_ERR_INVALID_PARAM;
    }
    if (key_len != OWFS_KEY_SIZE) {
        return OWFS_ERR_KEY_INVALID;
    }
    owfs_status_t vw = owfs_volume_writable(sb);
    if (vw != OWFS_OK) {
        return vw;
    }

    ow_memset(sb->key_slot_1, 0, sizeof(sb->key_slot_1));
    ow_memcpy(sb->key_slot_1, key, OWFS_KEY_SIZE);
    sb->security_flags |= OWFS_SEC_ENCRYPTED;
    owfs_status_t res = owfs_superblock_write(dev, sb);
    if (res != OWFS_OK) {
        return res;
    }
    htl_flush_cache(dev);
    return OWFS_OK;
}

owfs_status_t owfs_crypto_purge(htl_device_t *dev, owfs_superblock_t *sb) {
    if (!dev || !sb) {
        return OWFS_ERR_INVALID_PARAM;
    }

    ow_memset(sb->key_slot_1, 0xFF, sizeof(sb->key_slot_1));
    ow_memset(sb->key_slot_2, 0xFF, sizeof(sb->key_slot_2));
    owfs_status_t res = owfs_superblock_write(dev, sb);
    if (res != OWFS_OK) return res;
    htl_flush_cache(dev);

    ow_memset(sb->key_slot_1, 0x00, sizeof(sb->key_slot_1));
    ow_memset(sb->key_slot_2, 0x00, sizeof(sb->key_slot_2));
    res = owfs_superblock_write(dev, sb);
    if (res != OWFS_OK) return res;
    htl_flush_cache(dev);

    ow_memset(sb->key_slot_1, 0xAA, sizeof(sb->key_slot_1));
    ow_memset(sb->key_slot_2, 0xAA, sizeof(sb->key_slot_2));
    res = owfs_superblock_write(dev, sb);
    if (res != OWFS_OK) return res;
    htl_flush_cache(dev);

    sb->security_flags &= ~OWFS_SEC_ENCRYPTED;
    return owfs_sync_changes(dev, sb);
}
