#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "../include/usfs_entry.h"
#include "../../common/include/ow_checksum.h"
#include "../../common/include/ow_mem.h"
#include "../../common/include/ow_string.h"

uint32_t usfs_entry_compute_checksum(const usfs_entry_t *entry) {
    if (!entry) {
        return 0;
    }
    return ow_crc32c_struct(entry, sizeof(usfs_entry_t), offsetof(usfs_entry_t, checksum));
}

bool usfs_entry_verify_checksum(const usfs_entry_t *entry) {
    if (!entry) {
        return false;
    }
    return entry->checksum == usfs_entry_compute_checksum(entry);
}

static uint32_t entry_block_offset(const usfs_superblock_t *sb, uint32_t entry_idx) {
    uint32_t block_idx = entry_idx >> 4;
    return sb->entry_table_start + block_idx;
}

static uint32_t entry_slot_index(uint32_t entry_idx) {
    return entry_idx & 0x0F;
}

usfs_status_t usfs_entry_read(htl_device_t *dev, const usfs_superblock_t *sb, uint32_t entry_idx, usfs_entry_t *entry) {
    if (!dev || !sb || !entry) {
        return USFS_ERR_INVALID_PARAM;
    }
    if (entry_idx >= sb->total_entries) {
        return USFS_ERR_NOT_FOUND;
    }

    static uint8_t block_buf[USFS_BLOCK_SIZE];
    uint32_t block_num = entry_block_offset(sb, entry_idx);
    htl_status_t hres = htl_read_block(dev, block_num, block_buf);
    if (hres != HTL_OK) {
        return USFS_ERR_IO;
    }

    uint32_t idx = entry_slot_index(entry_idx);
    const usfs_entry_t *src = (const usfs_entry_t *)(block_buf + (idx * USFS_ENTRY_SIZE));
    ow_memcpy(entry, src, sizeof(usfs_entry_t));

    /* Unallocated/zero entry slot is valid empty entry */
    if (entry->entry_type == 0) {
        return USFS_OK;
    }

    if (!usfs_entry_verify_checksum(entry)) {
        return USFS_ERR_CHECKSUM_MISMATCH;
    }
    return USFS_OK;
}

usfs_status_t usfs_entry_write(htl_device_t *dev, const usfs_superblock_t *sb, uint32_t entry_idx, usfs_entry_t *entry) {
    if (!dev || !sb || !entry) {
        return USFS_ERR_INVALID_PARAM;
    }
    if (entry_idx >= sb->total_entries) {
        return USFS_ERR_NOT_FOUND;
    }

    static uint8_t block_buf[USFS_BLOCK_SIZE];
    uint32_t block_num = entry_block_offset(sb, entry_idx);
    htl_status_t hres = htl_read_block(dev, block_num, block_buf);
    if (hres != HTL_OK) {
        return USFS_ERR_IO;
    }

    entry->checksum = usfs_entry_compute_checksum(entry);
    uint32_t idx = entry_slot_index(entry_idx);
    usfs_entry_t *dst = (usfs_entry_t *)(block_buf + (idx * USFS_ENTRY_SIZE));
    ow_memcpy(dst, entry, sizeof(usfs_entry_t));

    hres = htl_write_block(dev, block_num, block_buf);
    if (hres != HTL_OK) {
        if (hres == HTL_ERR_WRITE_PROTECT) {
            return USFS_ERR_WRITE_PROTECTED;
        }
        return USFS_ERR_IO;
    }
    return USFS_OK;
}

usfs_status_t usfs_entry_alloc(htl_device_t *dev, usfs_superblock_t *sb, uint32_t *out_entry_idx) {
    if (!dev || !sb || !out_entry_idx) {
        return USFS_ERR_INVALID_PARAM;
    }
    if (sb->used_entries >= sb->total_entries) {
        return USFS_ERR_NO_FREE_ENTRIES;
    }

    usfs_entry_t temp;
    for (uint32_t i = 0; i < sb->total_entries; ++i) {
        usfs_status_t res = usfs_entry_read(dev, sb, i, &temp);
        if (res == USFS_OK) {
            if (temp.entry_type == 0 || (temp.entry_type & USFS_ENTRY_DELETED)) {
                ow_memset(&temp, 0, sizeof(usfs_entry_t));
                temp.entry_index = i;
                temp.entry_type = USFS_ENTRY_FILE;
                temp.checksum = usfs_entry_compute_checksum(&temp);

                res = usfs_entry_write(dev, sb, i, &temp);
                if (res != USFS_OK) {
                    return res;
                }
                sb->used_entries++;
                *out_entry_idx = i;
                return USFS_OK;
            }
        }
    }
    return USFS_ERR_NO_FREE_ENTRIES;
}

usfs_status_t usfs_entry_free(htl_device_t *dev, usfs_superblock_t *sb, uint32_t entry_idx) {
    if (!dev || !sb) {
        return USFS_ERR_INVALID_PARAM;
    }
    usfs_entry_t temp;
    usfs_status_t res = usfs_entry_read(dev, sb, entry_idx, &temp);
    if (res != USFS_OK) {
        return res;
    }
    temp.entry_type |= USFS_ENTRY_DELETED;
    res = usfs_entry_write(dev, sb, entry_idx, &temp);
    if (res == USFS_OK && sb->used_entries > 0) {
        sb->used_entries--;
    }
    return res;
}

usfs_status_t usfs_entry_lookup(htl_device_t *dev, const usfs_superblock_t *sb, uint32_t parent_idx, const uint8_t *name, size_t name_len, uint32_t *out_entry_idx) {
    if (!dev || !sb || !name || !out_entry_idx || name_len == 0) {
        return USFS_ERR_INVALID_PARAM;
    }
    if (name_len >= USFS_NAME_MAX_BYTES) {
        return USFS_ERR_NAME_TOO_LONG;
    }

    usfs_entry_t temp;
    for (uint32_t i = 0; i < sb->total_entries; ++i) {
        if (usfs_entry_read(dev, sb, i, &temp) == USFS_OK) {
            if (temp.entry_type != 0 && !(temp.entry_type & USFS_ENTRY_DELETED)) {
                if (temp.parent_entry == parent_idx) {
                    if (ow_sutf8_name_cmp(temp.name, temp.name_length, name, name_len) == 0) {
                        *out_entry_idx = i;
                        return USFS_OK;
                    }
                }
            }
        }
    }
    return USFS_ERR_NOT_FOUND;
}
