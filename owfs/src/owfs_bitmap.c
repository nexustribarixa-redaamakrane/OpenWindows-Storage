#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "../include/owfs_bitmap.h"
#include "../../common/include/ow_mem.h"

static bool bitmap_bounds_ok(const owfs_superblock_t *sb, uint32_t rel_block) {
    return rel_block <= (sb->total_blocks - 1 - sb->data_region_start);
}

owfs_status_t owfs_bitmap_init(htl_device_t *dev, const owfs_superblock_t *sb) {
    if (!dev || !sb) {
        return OWFS_ERR_INVALID_PARAM;
    }
    if (sb->bitmap_start_block == 0 || sb->bitmap_block_count == 0) {
        return OWFS_ERR_INVALID_PARAM;
    }
    static uint8_t zero_block[OWFS_BLOCK_SIZE];
    ow_memset(zero_block, 0, sizeof(zero_block));

    for (uint32_t i = 0; i < sb->bitmap_block_count; ++i) {
        uint32_t bnum = sb->bitmap_start_block + i;
        if (bnum >= sb->data_region_start) {
            return OWFS_ERR_INVALID_PARAM;
        }
        htl_status_t hres = htl_write_block(dev, bnum, zero_block);
        if (hres != HTL_OK) {
            return (hres == HTL_ERR_WRITE_PROTECT) ? OWFS_ERR_WRITE_PROTECTED : OWFS_ERR_IO;
        }
    }
    return OWFS_OK;
}

uint64_t owfs_bitmap_compute_fletcher64(htl_device_t *dev, const owfs_superblock_t *sb) {
    if (!dev || !sb) {
        return 0;
    }
    static uint8_t block_buf[OWFS_BLOCK_SIZE];
    uint64_t sum1 = 0;
    uint64_t sum2 = 0;

    for (uint32_t b = 0; b < sb->bitmap_block_count; ++b) {
        if (htl_read_block(dev, sb->bitmap_start_block + b, block_buf) != HTL_OK) {
            return 0;
        }
        /* Byte-wise little-endian words, mirroring ow_fletcher64. */
        size_t i = 0;
        while (i + 4 <= OWFS_BLOCK_SIZE) {
            uint32_t word = (uint32_t)block_buf[i] |
                            ((uint32_t)block_buf[i + 1] << 8) |
                            ((uint32_t)block_buf[i + 2] << 16) |
                            ((uint32_t)block_buf[i + 3] << 24);
            sum1 = (sum1 + word) % 0xFFFFFFFFULL;
            sum2 = (sum2 + sum1) % 0xFFFFFFFFULL;
            i += 4;
        }
    }
    return (sum2 << 32) | sum1;
}

uint32_t owfs_bitmap_count_free(htl_device_t *dev, const owfs_superblock_t *sb) {
    if (!dev || !sb) {
        return 0;
    }
    static uint8_t block_buf[OWFS_BLOCK_SIZE];
    uint32_t free_count = 0;
    uint32_t data_blocks = sb->total_blocks - sb->data_region_start;

    for (uint32_t b = 0; b < sb->bitmap_block_count; ++b) {
        if (htl_read_block(dev, sb->bitmap_start_block + b, block_buf) != HTL_OK) {
            return 0;
        }
        for (uint32_t byte_idx = 0; byte_idx < OWFS_BLOCK_SIZE; ++byte_idx) {
            for (int bit = 0; bit < 8; ++bit) {
                uint32_t rel = (b * OWFS_BLOCK_SIZE + byte_idx) * 8 + bit;
                if (rel >= data_blocks) {
                    break;
                }
                if (!(block_buf[byte_idx] & (1U << bit))) {
                    free_count++;
                }
            }
        }
    }
    return free_count;
}

owfs_status_t owfs_bitmap_alloc(htl_device_t *dev, owfs_superblock_t *sb, uint32_t *out_block) {
    if (!dev || !sb || !out_block) {
        return OWFS_ERR_INVALID_PARAM;
    }
    if (sb->free_blocks == 0) {
        return OWFS_ERR_NO_FREE_BLOCKS;
    }

    static uint8_t block_buf[OWFS_BLOCK_SIZE];

    for (uint32_t b = 0; b < sb->bitmap_block_count; ++b) {
        uint32_t bnum = sb->bitmap_start_block + b;
        htl_status_t hres = htl_read_block(dev, bnum, block_buf);
        if (hres != HTL_OK) {
            return OWFS_ERR_IO;
        }

        for (uint32_t byte_idx = 0; byte_idx < OWFS_BLOCK_SIZE; ++byte_idx) {
            if (block_buf[byte_idx] != 0xFF) {
                for (int bit = 0; bit < 8; ++bit) {
                    if (!(block_buf[byte_idx] & (1U << bit))) {
                        uint32_t rel = (b * OWFS_BLOCK_SIZE + byte_idx) * 8 + bit;
                        if (rel > (sb->total_blocks - 1 - sb->data_region_start)) {
                            return OWFS_ERR_NO_FREE_BLOCKS;
                        }
                        uint32_t block = sb->data_region_start + rel;
                        block_buf[byte_idx] |= (1U << bit);
                        hres = htl_write_block(dev, bnum, block_buf);
                        if (hres != HTL_OK) {
                            return (hres == HTL_ERR_WRITE_PROTECT) ? OWFS_ERR_WRITE_PROTECTED : OWFS_ERR_IO;
                        }
                        sb->free_blocks--;
                        *out_block = block;
                        return OWFS_OK;
                    }
                }
            }
        }
    }
    return OWFS_ERR_NO_FREE_BLOCKS;
}

owfs_status_t owfs_bitmap_free(htl_device_t *dev, owfs_superblock_t *sb, uint32_t block) {
    if (!dev || !sb) {
        return OWFS_ERR_INVALID_PARAM;
    }
    if (block < sb->data_region_start || block >= sb->total_blocks) {
        return OWFS_ERR_INVALID_PARAM;
    }

    uint32_t rel_block = block - sb->data_region_start;
    uint32_t bit_index = rel_block & 0x07;
    uint32_t byte_offset = rel_block >> 3;
    uint32_t bitmap_block = sb->bitmap_start_block + (byte_offset >> OWFS_BLOCK_SHIFT);
    if (!bitmap_bounds_ok(sb, rel_block)) {
        return OWFS_ERR_INVALID_PARAM;
    }

    static uint8_t block_buf[OWFS_BLOCK_SIZE];
    htl_status_t hres = htl_read_block(dev, bitmap_block, block_buf);
    if (hres != HTL_OK) {
        return OWFS_ERR_IO;
    }

    uint32_t in_block = byte_offset & (OWFS_BLOCK_SIZE - 1);
    if (block_buf[in_block] & (1U << bit_index)) {
        block_buf[in_block] &= ~(1U << bit_index);
        hres = htl_write_block(dev, bitmap_block, block_buf);
        if (hres != HTL_OK) {
            return (hres == HTL_ERR_WRITE_PROTECT) ? OWFS_ERR_WRITE_PROTECTED : OWFS_ERR_IO;
        }
        sb->free_blocks++;
    }
    return OWFS_OK;
}

bool owfs_bitmap_is_used(htl_device_t *dev, const owfs_superblock_t *sb, uint32_t block) {
    if (!dev || !sb || block < sb->data_region_start || block >= sb->total_blocks) {
        return false;
    }
    uint32_t rel_block = block - sb->data_region_start;
    uint32_t bit_index = rel_block & 0x07;
    uint32_t byte_offset = rel_block >> 3;
    uint32_t bitmap_block = sb->bitmap_start_block + (byte_offset >> OWFS_BLOCK_SHIFT);

    static uint8_t block_buf[OWFS_BLOCK_SIZE];
    if (htl_read_block(dev, bitmap_block, block_buf) != HTL_OK) {
        return false;
    }
    uint32_t in_block = byte_offset & (OWFS_BLOCK_SIZE - 1);
    return (block_buf[in_block] & (1U << bit_index)) != 0;
}
