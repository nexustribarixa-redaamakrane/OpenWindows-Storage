#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "../include/owfs_format.h"
#include "../include/owfs_inode.h"
#include "../include/owfs_bitmap.h"
#include "../include/owfs_sync.h"
#include "../../common/include/ow_mem.h"
#include "../../common/include/ow_string.h"

owfs_status_t owfs_format_volume(htl_device_t *dev, uint32_t total_blocks, const uint8_t *label, size_t label_len) {
    if (!dev || total_blocks < 64) {
        return OWFS_ERR_INVALID_PARAM;
    }

    /* 1. Build fresh superblock */
    static owfs_superblock_t sb;
    ow_memset(&sb, 0, sizeof(owfs_superblock_t));

    sb.magic = OWFS_MAGIC;
    sb.version_major = OWFS_VERSION_MAJOR;
    sb.version_minor = OWFS_VERSION_MINOR;
    sb.block_size = OWFS_BLOCK_SIZE;
    sb.total_blocks = total_blocks;

    /* Reserve 16 blocks for bitmap */
    sb.bitmap_start_block = 1;
    sb.bitmap_block_count = 1;

    /* Reserve 16 blocks for inode table (256 inodes) */
    sb.inode_table_start = sb.bitmap_start_block + sb.bitmap_block_count;
    sb.inode_table_blocks = 16;
    sb.total_inodes = sb.inode_table_blocks * (OWFS_BLOCK_SIZE / OWFS_INODE_SIZE);
    sb.free_inodes = sb.total_inodes;

    /* Data region starts immediately after inode table */
    sb.data_region_start = sb.inode_table_start + sb.inode_table_blocks;
    if (total_blocks <= sb.data_region_start) {
        return OWFS_ERR_INVALID_PARAM;
    }
    sb.free_blocks = total_blocks - sb.data_region_start;
    sb.root_inode = OWFS_ROOT_INODE;
    sb.state_flags = OWFS_STATE_CLEAN;

    if (label && label_len > 0) {
        ow_sutf8_name_copy(sb.volume_label, sizeof(sb.volume_label), label, label_len);
    }

    /* 2. Zero bitmap region */
    owfs_status_t res = owfs_bitmap_init(dev, &sb);
    if (res != OWFS_OK) return res;

    /* 3. Initialize inode table */
    static uint8_t zero_block[OWFS_BLOCK_SIZE];
    ow_memset(zero_block, 0, sizeof(zero_block));
    for (uint32_t i = 0; i < sb.inode_table_blocks; ++i) {
        htl_status_t hres = htl_write_block(dev, sb.inode_table_start + i, zero_block);
        if (hres != HTL_OK) return OWFS_ERR_IO;
    }

    /* 4. Allocate Inode 0 as Root Catalog ('/') */
    owfs_inode_t root_ino;
    ow_memset(&root_ino, 0, sizeof(owfs_inode_t));
    root_ino.inode_number = OWFS_ROOT_INODE;
    root_ino.entry_type = OWFS_ENTRY_CATALOG;
    root_ino.permissions = 075; /* rwxr-xr-x in 8-bit mode */
    root_ino.parent_inode = OWFS_ROOT_INODE;
    root_ino.name_length = 1;
    root_ino.name[0] = '/';

    /* Allocate first data block for root catalog */
    uint32_t root_block = 0;
    res = owfs_bitmap_alloc(dev, &sb, &root_block);
    if (res != OWFS_OK) return res;

    htl_zero_block(dev, root_block);
    root_ino.direct_blocks[0] = root_block;
    root_ino.block_count = 1;

    res = owfs_inode_write(dev, &sb, OWFS_ROOT_INODE, &root_ino);
    if (res != OWFS_OK) return res;

    /* 5. Synchronize all metadata changes to disk */
    return owfs_sync_changes(dev, &sb);
}

owfs_status_t owfs_quick_format(htl_device_t *dev, uint32_t total_blocks, const uint8_t *label, size_t label_len) {
    return owfs_format_volume(dev, total_blocks, label, label_len);
}

owfs_status_t owfs_full_scrub(htl_device_t *dev, uint32_t total_blocks, const uint8_t *label, size_t label_len) {
    if (!dev || total_blocks < 64) {
        return OWFS_ERR_INVALID_PARAM;
    }
    for (uint32_t b = 0; b < total_blocks; ++b) {
        htl_status_t hres = htl_zero_block(dev, b);
        if (hres != HTL_OK) return OWFS_ERR_IO;
    }
    return owfs_format_volume(dev, total_blocks, label, label_len);
}
