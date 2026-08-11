#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "../include/owfs_blockmap.h"
#include "../include/owfs_bitmap.h"
#include "../include/owfs_sync.h"
#include "../../common/include/ow_mem.h"
#include "../../common/include/ow_htl.h"

static uint32_t ptr_load(const uint8_t *p) {
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static void ptr_store(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

owfs_status_t owfs_blockmap_get(htl_device_t *dev, const owfs_inode_t *inode,
                                uint32_t idx, uint32_t *out_block) {
    if (!dev || !inode || !out_block) {
        return OWFS_ERR_INVALID_PARAM;
    }
    if (idx >= inode->block_count) {
        return OWFS_ERR_INVALID_PARAM;
    }
    if (idx < OWFS_DIRECT_BLOCKS) {
        if (inode->direct_blocks[idx] == 0) {
            return OWFS_ERR_INVALID_PARAM;
        }
        *out_block = inode->direct_blocks[idx];
        return OWFS_OK;
    }
    if (inode->indirect_block == 0) {
        return OWFS_ERR_INVALID_PARAM;
    }
    static uint8_t buf[OWFS_BLOCK_SIZE];
    if (htl_read_block(dev, inode->indirect_block, buf) != HTL_OK) {
        return OWFS_ERR_IO;
    }
    uint32_t entry = idx - OWFS_DIRECT_BLOCKS;
    uint32_t b = ptr_load(buf + (entry * 4));
    if (b == 0) {
        return OWFS_ERR_INVALID_PARAM;
    }
    *out_block = b;
    return OWFS_OK;
}

static owfs_status_t alloc_and_zero(htl_device_t *dev, owfs_superblock_t *sb,
                                    uint32_t *out_block) {
    uint32_t b = 0;
    owfs_status_t res = owfs_bitmap_alloc(dev, sb, &b);
    if (res != OWFS_OK) {
        return res;
    }
    htl_status_t hres = htl_zero_block(dev, b);
    if (hres != HTL_OK) {
        owfs_bitmap_free(dev, sb, b);
        return (hres == HTL_ERR_WRITE_PROTECT) ? OWFS_ERR_WRITE_PROTECTED : OWFS_ERR_IO;
    }
    *out_block = b;
    return OWFS_OK;
}

owfs_status_t owfs_blockmap_ensure(htl_device_t *dev, owfs_superblock_t *sb,
                                   owfs_inode_t *inode, uint32_t idx, uint32_t *out_block) {
    if (!dev || !sb || !inode || !out_block) {
        return OWFS_ERR_INVALID_PARAM;
    }
    if (idx >= OWFS_MAX_LOGICAL_BLOCKS) {
        return OWFS_ERR_NO_FREE_BLOCKS;
    }
    if (owfs_volume_writable(sb) != OWFS_OK) {
        return OWFS_ERR_VOLUME_DIRTY;
    }
    if (idx < inode->block_count) {
        return owfs_blockmap_get(dev, inode, idx, out_block);
    }
    if (idx != inode->block_count) {
        return OWFS_ERR_INVALID_PARAM; /* sparse writes not supported */
    }

    uint32_t data_block = 0;
    owfs_status_t res = alloc_and_zero(dev, sb, &data_block);
    if (res != OWFS_OK) {
        return res;
    }

    if (idx < OWFS_DIRECT_BLOCKS) {
        inode->direct_blocks[idx] = data_block;
    } else {
        static uint8_t ibuf[OWFS_BLOCK_SIZE];
        if (inode->indirect_block == 0) {
            uint32_t ind = 0;
            res = alloc_and_zero(dev, sb, &ind);
            if (res != OWFS_OK) {
                owfs_bitmap_free(dev, sb, data_block);
                return res;
            }
            inode->indirect_block = ind;
            ow_memset(ibuf, 0, sizeof(ibuf));
        } else if (htl_read_block(dev, inode->indirect_block, ibuf) != HTL_OK) {
            owfs_bitmap_free(dev, sb, data_block);
            return OWFS_ERR_IO;
        }
        uint32_t entry = idx - OWFS_DIRECT_BLOCKS;
        ptr_store(ibuf + (entry * 4), data_block);
        htl_status_t hres = htl_write_block(dev, inode->indirect_block, ibuf);
        if (hres != HTL_OK) {
            owfs_bitmap_free(dev, sb, data_block);
            if (inode->block_count == OWFS_DIRECT_BLOCKS && entry == 0) {
                /* The indirect block was just created and now has no pointers. */
                owfs_bitmap_free(dev, sb, inode->indirect_block);
                inode->indirect_block = 0;
            }
            return (hres == HTL_ERR_WRITE_PROTECT) ? OWFS_ERR_WRITE_PROTECTED : OWFS_ERR_IO;
        }
    }

    inode->block_count++;
    *out_block = data_block;
    return OWFS_OK;
}

owfs_status_t owfs_blockmap_release(htl_device_t *dev, owfs_superblock_t *sb,
                                    owfs_inode_t *inode, uint32_t from_idx) {
    if (!dev || !sb || !inode) {
        return OWFS_ERR_INVALID_PARAM;
    }
    if (owfs_volume_writable(sb) != OWFS_OK) {
        return OWFS_ERR_VOLUME_DIRTY;
    }
    if (from_idx >= inode->block_count) {
        return OWFS_OK;
    }

    static uint8_t ibuf[OWFS_BLOCK_SIZE];
    bool indirect_loaded = false;

    for (uint32_t idx = from_idx; idx < inode->block_count; ++idx) {
        uint32_t b = 0;
        if (idx < OWFS_DIRECT_BLOCKS) {
            b = inode->direct_blocks[idx];
        } else {
            if (inode->indirect_block != 0) {
                if (!indirect_loaded) {
                    if (htl_read_block(dev, inode->indirect_block, ibuf) != HTL_OK) {
                        return OWFS_ERR_IO;
                    }
                    indirect_loaded = true;
                }
                b = ptr_load(ibuf + ((idx - OWFS_DIRECT_BLOCKS) * 4));
            }
        }
        if (b != 0) {
            owfs_status_t res = owfs_bitmap_free(dev, sb, b);
            if (res != OWFS_OK) {
                return res;
            }
        }
    }

    /* Clear released direct pointers. */
    for (uint32_t idx = from_idx; idx < inode->block_count && idx < OWFS_DIRECT_BLOCKS; ++idx) {
        inode->direct_blocks[idx] = 0;
    }

    /* Determine whether any indirect pointers remain. */
    uint32_t new_block_count = from_idx;
    bool indirect_in_use = new_block_count > OWFS_DIRECT_BLOCKS;
    if (!indirect_in_use && inode->indirect_block != 0) {
        if (indirect_loaded) {
            ow_memset(ibuf, 0, sizeof(ibuf));
        }
        owfs_status_t res = owfs_bitmap_free(dev, sb, inode->indirect_block);
        if (res != OWFS_OK) {
            return res;
        }
        inode->indirect_block = 0;
    }

    inode->block_count = new_block_count;
    return OWFS_OK;
}
