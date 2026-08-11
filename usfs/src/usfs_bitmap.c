#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "../include/usfs_bitmap.h"
#include "../include/usfs_sync.h"
#include "../../common/include/ow_mem.h"

#define USFS_BITS_PER_BLOCK (USFS_BLOCK_SIZE * 8)

static uint32_t data_block_count(const usfs_superblock_t *sb) {
    return sb->total_blocks - sb->data_region_start;
}

static bool bit_is_set(const uint8_t *buf, uint32_t rel) {
    uint32_t byte_idx = rel >> 3;
    uint32_t bit = rel & 7;
    return (buf[byte_idx] & (1U << bit)) != 0;
}

static void bit_set(uint8_t *buf, uint32_t rel, bool used) {
    uint32_t byte_idx = rel >> 3;
    uint32_t bit = rel & 7;
    if (used) {
        buf[byte_idx] |= (1U << bit);
    } else {
        buf[byte_idx] &= (uint8_t)~(1U << bit);
    }
}

usfs_status_t usfs_bitmap_init(htl_device_t *dev, const usfs_superblock_t *sb) {
    if (!dev || !sb) {
        return USFS_ERR_INVALID_PARAM;
    }
    if (sb->bitmap_start_block == 0 || sb->bitmap_block_count == 0) {
        return USFS_ERR_INVALID_PARAM;
    }
    static uint8_t zero_block[USFS_BLOCK_SIZE];
    ow_memset(zero_block, 0, sizeof(zero_block));
    for (uint32_t i = 0; i < sb->bitmap_block_count; ++i) {
        uint32_t bnum = sb->bitmap_start_block + i;
        if (bnum >= sb->data_region_start) {
            return USFS_ERR_INVALID_PARAM;
        }
        htl_status_t hres = htl_write_block(dev, bnum, zero_block);
        if (hres != HTL_OK) {
            return (hres == HTL_ERR_WRITE_PROTECT) ? USFS_ERR_WRITE_PROTECTED : USFS_ERR_IO;
        }
    }
    return USFS_OK;
}

usfs_status_t usfs_block_alloc(htl_device_t *dev, usfs_superblock_t *sb, uint32_t count, uint32_t *out_first) {
    if (!dev || !sb || !out_first) {
        return USFS_ERR_INVALID_PARAM;
    }
    if (count == 0 || count > sb->free_blocks) {
        return USFS_ERR_NO_FREE_BLOCKS;
    }
    if (sb->total_blocks <= sb->data_region_start) {
        return USFS_ERR_NO_FREE_BLOCKS;
    }
    if (usfs_volume_writable(sb) != USFS_OK) {
        return USFS_ERR_VOLUME_DIRTY;
    }

    static uint8_t buf[USFS_BLOCK_SIZE];
    uint32_t total = data_block_count(sb);
    uint32_t run_start = 0;
    uint32_t run_len = 0;
    uint32_t current_block = 0xFFFFFFFFU;

    for (uint32_t rel = 0; rel < total; ++rel) {
        uint32_t blk = rel / USFS_BITS_PER_BLOCK;
        if (blk != current_block) {
            uint32_t bnum = sb->bitmap_start_block + blk;
            if (htl_read_block(dev, bnum, buf) != HTL_OK) {
                return USFS_ERR_IO;
            }
            current_block = blk;
        }
        if (!bit_is_set(buf, rel % USFS_BITS_PER_BLOCK)) {
            if (run_len == 0) {
                run_start = rel;
            }
            run_len++;
            if (run_len == count) {
                break;
            }
        } else {
            run_len = 0;
        }
    }
    if (run_len != count) {
        return USFS_ERR_NO_FREE_BLOCKS;
    }

    /* Mark the run used, writing each affected bitmap block once. */
    uint32_t first_blk = run_start / USFS_BITS_PER_BLOCK;
    uint32_t last_blk = (run_start + count - 1) / USFS_BITS_PER_BLOCK;
    for (uint32_t blk = first_blk; blk <= last_blk; ++blk) {
        uint32_t bnum = sb->bitmap_start_block + blk;
        if (htl_read_block(dev, bnum, buf) != HTL_OK) {
            return USFS_ERR_IO;
        }
        uint32_t begin = (blk == first_blk) ? (run_start % USFS_BITS_PER_BLOCK) : 0;
        uint32_t end = (blk == last_blk) ? ((run_start + count - 1) % USFS_BITS_PER_BLOCK) : (USFS_BITS_PER_BLOCK - 1);
        for (uint32_t rel = begin; rel <= end; ++rel) {
            bit_set(buf, rel, true);
        }
        htl_status_t hres = htl_write_block(dev, bnum, buf);
        if (hres != HTL_OK) {
            return (hres == HTL_ERR_WRITE_PROTECT) ? USFS_ERR_WRITE_PROTECTED : USFS_ERR_IO;
        }
    }

    sb->free_blocks -= count;
    *out_first = sb->data_region_start + run_start;
    return USFS_OK;
}

usfs_status_t usfs_block_free(htl_device_t *dev, usfs_superblock_t *sb, uint32_t first, uint32_t count) {
    if (!dev || !sb) {
        return USFS_ERR_INVALID_PARAM;
    }
    if (count == 0) {
        return USFS_OK;
    }
    if (first < sb->data_region_start || (uint64_t)first + count > sb->total_blocks) {
        return USFS_ERR_INVALID_PARAM;
    }
    if (usfs_volume_writable(sb) != USFS_OK) {
        return USFS_ERR_VOLUME_DIRTY;
    }

    static uint8_t buf[USFS_BLOCK_SIZE];
    uint32_t rel_start = first - sb->data_region_start;
    uint32_t first_blk = rel_start / USFS_BITS_PER_BLOCK;
    uint32_t last_blk = (rel_start + count - 1) / USFS_BITS_PER_BLOCK;

    for (uint32_t blk = first_blk; blk <= last_blk; ++blk) {
        uint32_t bnum = sb->bitmap_start_block + blk;
        if (htl_read_block(dev, bnum, buf) != HTL_OK) {
            return USFS_ERR_IO;
        }
        uint32_t begin = (blk == first_blk) ? (rel_start % USFS_BITS_PER_BLOCK) : 0;
        uint32_t end = (blk == last_blk) ? ((rel_start + count - 1) % USFS_BITS_PER_BLOCK) : (USFS_BITS_PER_BLOCK - 1);
        for (uint32_t rel = begin; rel <= end; ++rel) {
            bit_set(buf, rel, false);
        }
        htl_status_t hres = htl_write_block(dev, bnum, buf);
        if (hres != HTL_OK) {
            return (hres == HTL_ERR_WRITE_PROTECT) ? USFS_ERR_WRITE_PROTECTED : USFS_ERR_IO;
        }
    }

    sb->free_blocks += count;
    return USFS_OK;
}

bool usfs_block_is_used(htl_device_t *dev, const usfs_superblock_t *sb, uint32_t block) {
    if (!dev || !sb || block < sb->data_region_start || block >= sb->total_blocks) {
        return false;
    }
    static uint8_t buf[USFS_BLOCK_SIZE];
    uint32_t rel = block - sb->data_region_start;
    uint32_t blk = rel / USFS_BITS_PER_BLOCK;
    if (htl_read_block(dev, sb->bitmap_start_block + blk, buf) != HTL_OK) {
        return false;
    }
    return bit_is_set(buf, rel % USFS_BITS_PER_BLOCK);
}

uint64_t usfs_bitmap_compute_fletcher64(htl_device_t *dev, const usfs_superblock_t *sb) {
    if (!dev || !sb) {
        return 0;
    }
    static uint8_t buf[USFS_BLOCK_SIZE];
    uint64_t sum1 = 0;
    uint64_t sum2 = 0;
    for (uint32_t b = 0; b < sb->bitmap_block_count; ++b) {
        if (htl_read_block(dev, sb->bitmap_start_block + b, buf) != HTL_OK) {
            return 0;
        }
        size_t i = 0;
        while (i + 4 <= USFS_BLOCK_SIZE) {
            uint32_t word = (uint32_t)buf[i] |
                            ((uint32_t)buf[i + 1] << 8) |
                            ((uint32_t)buf[i + 2] << 16) |
                            ((uint32_t)buf[i + 3] << 24);
            sum1 = (sum1 + word) % 0xFFFFFFFFULL;
            sum2 = (sum2 + sum1) % 0xFFFFFFFFULL;
            i += 4;
        }
    }
    return (sum2 << 32) | sum1;
}

uint32_t usfs_bitmap_count_free(htl_device_t *dev, const usfs_superblock_t *sb) {
    if (!dev || !sb) {
        return 0;
    }
    static uint8_t buf[USFS_BLOCK_SIZE];
    uint32_t free_count = 0;
    if (sb->total_blocks <= sb->data_region_start) {
        return 0;
    }
    uint32_t total = data_block_count(sb);
    uint32_t current_block = 0xFFFFFFFFU;

    for (uint32_t rel = 0; rel < total; ++rel) {
        uint32_t blk = rel / USFS_BITS_PER_BLOCK;
        if (blk != current_block) {
            if (htl_read_block(dev, sb->bitmap_start_block + blk, buf) != HTL_OK) {
                return 0;
            }
            current_block = blk;
        }
        if (!bit_is_set(buf, rel % USFS_BITS_PER_BLOCK)) {
            free_count++;
        }
    }
    return free_count;
}
