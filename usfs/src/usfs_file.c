#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "../include/usfs_file.h"
#include "../include/usfs_bitmap.h"
#include "../include/usfs_sync.h"
#include "../../common/include/ow_crypto.h"
#include "../../common/include/ow_mem.h"

static bool is_encrypted(const usfs_superblock_t *sb, const usfs_entry_t *entry) {
    return (sb->security_flags & USFS_SEC_ENCRYPTED) &&
           (entry->security_flags & USFS_SEC_ENCRYPTED);
}

static void crypto_xform(const usfs_superblock_t *sb, const usfs_entry_t *entry,
                         uint8_t *buf, uint32_t abs_block, bool encrypt) {
    (void)encrypt;
    if (is_encrypted(sb, entry)) {
        static const uint8_t zero_nonce[OW_CHACHA20_NONCE_SIZE] = {0};
        ow_chacha20_xor(abs_block, sb->key_slot_1, zero_nonce, buf, USFS_BLOCK_SIZE);
    }
}

static usfs_status_t grow_file(htl_device_t *dev, usfs_superblock_t *sb,
                               usfs_entry_t *entry, uint32_t entry_idx,
                               uint32_t need_blocks) {
    if (entry->block_count >= need_blocks) {
        return USFS_OK;
    }

    uint32_t new_first = 0;
    usfs_status_t res = usfs_block_alloc(dev, sb, need_blocks, &new_first);
    if (res != USFS_OK) {
        return res;
    }

    static uint8_t buf[USFS_BLOCK_SIZE];
    uint32_t old_first = entry->first_block;
    uint32_t old_count = entry->block_count;
    for (uint32_t i = 0; i < old_count; ++i) {
        if (htl_read_block(dev, old_first + i, buf) != HTL_OK) {
            usfs_block_free(dev, sb, new_first, need_blocks);
            return USFS_ERR_IO;
        }
        crypto_xform(sb, entry, buf, old_first + i, true);
        crypto_xform(sb, entry, buf, new_first + i, true);
        htl_status_t hres = htl_write_block(dev, new_first + i, buf);
        if (hres != HTL_OK) {
            usfs_block_free(dev, sb, new_first, need_blocks);
            return (hres == HTL_ERR_WRITE_PROTECT) ? USFS_ERR_WRITE_PROTECTED : USFS_ERR_IO;
        }
    }

    if (old_count > 0) {
        res = usfs_block_free(dev, sb, old_first, old_count);
        if (res != USFS_OK) {
            usfs_block_free(dev, sb, new_first, need_blocks);
            return res;
        }
    }

    entry->first_block = new_first;
    entry->block_count = need_blocks;
    return usfs_entry_write(dev, sb, entry_idx, entry);
}

usfs_status_t usfs_file_read(htl_device_t *dev, const usfs_superblock_t *sb,
                             const usfs_entry_t *entry,
                             uint8_t *buf, uint32_t offset, uint32_t len,
                             uint32_t *out_read) {
    if (!dev || !sb || !entry || !buf || !out_read) {
        return USFS_ERR_INVALID_PARAM;
    }
    if (entry->entry_type != USFS_ENTRY_FILE) {
        return USFS_ERR_NOT_FILE;
    }
    *out_read = 0;
    if (entry->block_count == 0 || offset >= entry->size_bytes) {
        return USFS_OK;
    }
    if (len > entry->size_bytes - offset) {
        len = entry->size_bytes - offset;
    }

    static uint8_t block_buf[USFS_BLOCK_SIZE];
    uint32_t done = 0;
    uint32_t off = offset;
    while (done < len) {
        uint32_t blk_abs = entry->first_block + (off >> USFS_BLOCK_SHIFT);
        uint32_t in_blk = off & (USFS_BLOCK_SIZE - 1);
        uint32_t chunk = len - done;
        if (chunk > USFS_BLOCK_SIZE - in_blk) {
            chunk = USFS_BLOCK_SIZE - in_blk;
        }
        if (htl_read_block(dev, blk_abs, block_buf) != HTL_OK) {
            return USFS_ERR_IO;
        }
        crypto_xform(sb, entry, block_buf, blk_abs, false);
        ow_memcpy(buf + done, block_buf + in_blk, chunk);
        done += chunk;
        off += chunk;
    }
    *out_read = len;
    return USFS_OK;
}

usfs_status_t usfs_file_write(htl_device_t *dev, usfs_superblock_t *sb,
                              usfs_entry_t *entry, uint32_t entry_idx,
                              const uint8_t *buf, uint32_t offset, uint32_t len,
                              uint32_t *out_written) {
    if (!dev || !sb || !entry || !buf || !out_written) {
        return USFS_ERR_INVALID_PARAM;
    }
    if (entry->entry_type != USFS_ENTRY_FILE) {
        return USFS_ERR_NOT_FILE;
    }
    if (usfs_volume_writable(sb) != USFS_OK) {
        return USFS_ERR_VOLUME_DIRTY;
    }
    *out_written = 0;
    if (len == 0) {
        return USFS_OK;
    }

    uint64_t end = (uint64_t)offset + len;
    uint32_t need_blocks = (uint32_t)((end + USFS_BLOCK_SIZE - 1) >> USFS_BLOCK_SHIFT);
    usfs_status_t res = grow_file(dev, sb, entry, entry_idx, need_blocks);
    if (res != USFS_OK) {
        return res;
    }

    static uint8_t block_buf[USFS_BLOCK_SIZE];
    uint32_t done = 0;
    uint32_t off = offset;
    while (done < len) {
        uint32_t blk_abs = entry->first_block + (off >> USFS_BLOCK_SHIFT);
        uint32_t in_blk = off & (USFS_BLOCK_SIZE - 1);
        uint32_t chunk = len - done;
        if (chunk > USFS_BLOCK_SIZE - in_blk) {
            chunk = USFS_BLOCK_SIZE - in_blk;
        }
        if (htl_read_block(dev, blk_abs, block_buf) != HTL_OK) {
            return USFS_ERR_IO;
        }
        crypto_xform(sb, entry, block_buf, blk_abs, false);
        ow_memcpy(block_buf + in_blk, buf + done, chunk);
        crypto_xform(sb, entry, block_buf, blk_abs, true);
        htl_status_t hres = htl_write_block(dev, blk_abs, block_buf);
        if (hres != HTL_OK) {
            return (hres == HTL_ERR_WRITE_PROTECT) ? USFS_ERR_WRITE_PROTECTED : USFS_ERR_IO;
        }
        done += chunk;
        off += chunk;
    }

    *out_written = done;
    entry->size_bytes = (uint32_t)end;
    return usfs_entry_write(dev, sb, entry_idx, entry);
}

usfs_status_t usfs_file_truncate(htl_device_t *dev, usfs_superblock_t *sb,
                                 usfs_entry_t *entry, uint32_t entry_idx,
                                 uint32_t new_size) {
    if (!dev || !sb || !entry) {
        return USFS_ERR_INVALID_PARAM;
    }
    if (entry->entry_type != USFS_ENTRY_FILE) {
        return USFS_ERR_NOT_FILE;
    }
    if (usfs_volume_writable(sb) != USFS_OK) {
        return USFS_ERR_VOLUME_DIRTY;
    }
    if (new_size == entry->size_bytes) {
        return USFS_OK;
    }

    if (new_size > entry->size_bytes) {
        uint32_t need_blocks = (uint32_t)(((uint64_t)new_size + USFS_BLOCK_SIZE - 1) >> USFS_BLOCK_SHIFT);
        usfs_status_t res = grow_file(dev, sb, entry, entry_idx, need_blocks);
        if (res != USFS_OK) {
            return res;
        }
        entry->size_bytes = new_size;
        return usfs_entry_write(dev, sb, entry_idx, entry);
    }

    uint32_t new_blocks = (uint32_t)(((uint64_t)new_size + USFS_BLOCK_SIZE - 1) >> USFS_BLOCK_SHIFT);
    if (new_size == 0) {
        new_blocks = 0;
    }
    if (new_blocks < entry->block_count) {
        usfs_status_t res = usfs_block_free(dev, sb, entry->first_block + new_blocks, entry->block_count - new_blocks);
        if (res != USFS_OK) {
            return res;
        }
        entry->block_count = new_blocks;
    }
    entry->size_bytes = new_size;
    return usfs_entry_write(dev, sb, entry_idx, entry);
}
