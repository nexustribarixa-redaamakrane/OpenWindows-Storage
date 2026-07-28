#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "../include/owfs_bitmap.h"
#include "../../common/include/ow_mem.h"

owfs_status_t owfs_bitmap_init(htl_device_t *dev, const owfs_superblock_t *sb) {
    if (!dev || !sb) {
        return OWFS_ERR_INVALID_PARAM;
    }
    static uint8_t zero_block[OWFS_BLOCK_SIZE];
    ow_memset(zero_block, 0, sizeof(zero_block));

    for (uint32_t i = 0; i < sb->bitmap_block_count; ++i) {
        htl_status_t hres = htl_write_block(dev, sb->bitmap_start_block + i, zero_block);
        if (hres != HTL_OK) return OWFS_ERR_IO;
    }
    return OWFS_OK;
}

uint64_t owfs_bitmap_compute_fletcher64(htl_device_t *dev, const owfs_superblock_t *sb) {
    if (!dev || !sb) {
        return 0;
    }
    static uint8_t block_buf[OWFS_BLOCK_SIZE];
    uint64_t sum1 = 0, sum2 = 0;

    for (uint32_t b = 0; b < sb->bitmap_block_count; ++b) {
        if (htl_read_block(dev, sb->bitmap_start_block + b, block_buf) != HTL_OK) {
            return 0;
        }
        const uint32_t *words = (const uint32_t *)block_buf;
        size_t count = OWFS_BLOCK_SIZE >> 2;
        for (size_t i = 0; i < count; ++i) {
            sum1 = (sum1 + words[i]) % 0xFFFFFFFFULL;
            sum2 = (sum2 + sum1) % 0xFFFFFFFFULL;
        }
    }
    return (sum2 << 32) | sum1;
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
        if (hres != HTL_OK) return OWFS_ERR_IO;

        for (uint32_t byte_idx = 0; byte_idx < OWFS_BLOCK_SIZE; ++byte_idx) {
            if (block_buf[byte_idx] != 0xFF) {
                for (int bit = 0; bit < 8; ++bit) {
                    if (!(block_buf[byte_idx] & (1U << bit))) {
                        uint32_t block = sb->data_region_start + ((b * OWFS_BLOCK_SIZE + byte_idx) * 8 + bit);
                        if (block >= sb->total_blocks) {
                            return OWFS_ERR_NO_FREE_BLOCKS;
                        }
                        block_buf[byte_idx] |= (1U << bit);
                        hres = htl_write_block(dev, bnum, block_buf);
                        if (hres != HTL_OK) return OWFS_ERR_IO;

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
    uint32_t byte_offset = (rel_block >> 3) & (OWFS_BLOCK_SIZE - 1);
    uint32_t bitmap_block = sb->bitmap_start_block + ((rel_block >> 3) >> OWFS_BLOCK_SHIFT);

    static uint8_t block_buf[OWFS_BLOCK_SIZE];
    htl_status_t hres = htl_read_block(dev, bitmap_block, block_buf);
    if (hres != HTL_OK) return OWFS_ERR_IO;

    if (block_buf[byte_offset] & (1U << bit_index)) {
        block_buf[byte_offset] &= ~(1U << bit_index);
        hres = htl_write_block(dev, bitmap_block, block_buf);
        if (hres != HTL_OK) return OWFS_ERR_IO;
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
    uint32_t byte_offset = (rel_block >> 3) & (OWFS_BLOCK_SIZE - 1);
    uint32_t bitmap_block = sb->bitmap_start_block + ((rel_block >> 3) >> OWFS_BLOCK_SHIFT);

    static uint8_t block_buf[OWFS_BLOCK_SIZE];
    if (htl_read_block(dev, bitmap_block, block_buf) != HTL_OK) {
        return false;
    }
    return (block_buf[byte_offset] & (1U << bit_index)) != 0;
}
