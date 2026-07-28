#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "../include/owfs_catalog.h"
#include "../include/owfs_bitmap.h"
#include "../../common/include/ow_checksum.h"
#include "../../common/include/ow_mem.h"
#include "../../common/include/ow_string.h"

uint32_t owfs_catalog_entry_compute_checksum(const owfs_catalog_entry_t *entry) {
    if (!entry) {
        return 0;
    }
    return ow_crc32c_struct(entry, sizeof(owfs_catalog_entry_t), offsetof(owfs_catalog_entry_t, checksum));
}

bool owfs_catalog_entry_verify_checksum(const owfs_catalog_entry_t *entry) {
    if (!entry) {
        return false;
    }
    return entry->checksum == owfs_catalog_entry_compute_checksum(entry);
}

owfs_status_t owfs_catalog_lookup(htl_device_t *dev, const owfs_superblock_t *sb, uint32_t catalog_inode, const uint8_t *name, size_t name_len, uint32_t *out_inode) {
    if (!dev || !sb || !name || !out_inode || name_len == 0) {
        return OWFS_ERR_INVALID_PARAM;
    }
    if (name_len >= OWFS_NAME_MAX_BYTES) {
        return OWFS_ERR_NAME_TOO_LONG;
    }

    owfs_inode_t cinode;
    owfs_status_t res = owfs_inode_read(dev, sb, catalog_inode, &cinode);
    if (res != OWFS_OK) {
        return res;
    }
    if (!(cinode.entry_type & OWFS_ENTRY_CATALOG)) {
        return OWFS_ERR_NOT_CATALOG;
    }

    static uint8_t block_buf[OWFS_BLOCK_SIZE];

    for (size_t b = 0; b < OWFS_DIRECT_BLOCKS; ++b) {
        uint32_t bnum = cinode.direct_blocks[b];
        if (bnum == 0) continue;

        htl_status_t hres = htl_read_block(dev, bnum, block_buf);
        if (hres != HTL_OK) return OWFS_ERR_IO;

        for (size_t e = 0; e < (OWFS_BLOCK_SIZE / OWFS_CATALOG_ENTRY_SIZE); ++e) {
            const owfs_catalog_entry_t *centry = (const owfs_catalog_entry_t *)(block_buf + (e * OWFS_CATALOG_ENTRY_SIZE));
            if (centry->entry_type != 0 && !(centry->entry_type & OWFS_ENTRY_DELETED)) {
                if (owfs_catalog_entry_verify_checksum(centry)) {
                    if (ow_sutf8_name_cmp(centry->name, centry->name_length, name, name_len) == 0) {
                        *out_inode = centry->inode_number;
                        return OWFS_OK;
                    }
                }
            }
        }
    }
    return OWFS_ERR_NOT_FOUND;
}

owfs_status_t owfs_catalog_insert(htl_device_t *dev, owfs_superblock_t *sb, uint32_t catalog_inode, const uint8_t *name, size_t name_len, uint32_t target_inode, uint8_t entry_type) {
    if (!dev || !sb || !name || name_len == 0) {
        return OWFS_ERR_INVALID_PARAM;
    }
    if (name_len >= OWFS_NAME_MAX_BYTES) {
        return OWFS_ERR_NAME_TOO_LONG;
    }

    uint32_t existing = 0;
    if (owfs_catalog_lookup(dev, sb, catalog_inode, name, name_len, &existing) == OWFS_OK) {
        return OWFS_ERR_ALREADY_EXISTS;
    }

    owfs_inode_t cinode;
    owfs_status_t res = owfs_inode_read(dev, sb, catalog_inode, &cinode);
    if (res != OWFS_OK) {
        return res;
    }
    if (!(cinode.entry_type & OWFS_ENTRY_CATALOG)) {
        return OWFS_ERR_NOT_CATALOG;
    }

    static uint8_t block_buf[OWFS_BLOCK_SIZE];

    for (size_t b = 0; b < OWFS_DIRECT_BLOCKS; ++b) {
        uint32_t bnum = cinode.direct_blocks[b];
        if (bnum == 0) {
            res = owfs_bitmap_alloc(dev, sb, &bnum);
            if (res != OWFS_OK) return res;
            htl_zero_block(dev, bnum);
            cinode.direct_blocks[b] = bnum;
            cinode.block_count++;
        }

        htl_status_t hres = htl_read_block(dev, bnum, block_buf);
        if (hres != HTL_OK) return OWFS_ERR_IO;

        for (size_t e = 0; e < (OWFS_BLOCK_SIZE / OWFS_CATALOG_ENTRY_SIZE); ++e) {
            owfs_catalog_entry_t *centry = (owfs_catalog_entry_t *)(block_buf + (e * OWFS_CATALOG_ENTRY_SIZE));
            if (centry->entry_type == 0 || (centry->entry_type & OWFS_ENTRY_DELETED)) {
                ow_memset(centry, 0, sizeof(owfs_catalog_entry_t));
                centry->inode_number = target_inode;
                centry->entry_type = entry_type;
                centry->name_length = (uint8_t)name_len;
                ow_memcpy(centry->name, name, name_len);
                centry->checksum = owfs_catalog_entry_compute_checksum(centry);

                hres = htl_write_block(dev, bnum, block_buf);
                if (hres != HTL_OK) return OWFS_ERR_IO;

                return owfs_inode_write(dev, sb, catalog_inode, &cinode);
            }
        }
    }
    return OWFS_ERR_CATALOG_FULL;
}

owfs_status_t owfs_catalog_remove(htl_device_t *dev, owfs_superblock_t *sb, uint32_t catalog_inode, const uint8_t *name, size_t name_len) {
    if (!dev || !sb || !name || name_len == 0) {
        return OWFS_ERR_INVALID_PARAM;
    }
    owfs_inode_t cinode;
    owfs_status_t res = owfs_inode_read(dev, sb, catalog_inode, &cinode);
    if (res != OWFS_OK) return res;

    static uint8_t block_buf[OWFS_BLOCK_SIZE];

    for (size_t b = 0; b < OWFS_DIRECT_BLOCKS; ++b) {
        uint32_t bnum = cinode.direct_blocks[b];
        if (bnum == 0) continue;

        htl_status_t hres = htl_read_block(dev, bnum, block_buf);
        if (hres != HTL_OK) return OWFS_ERR_IO;

        for (size_t e = 0; e < (OWFS_BLOCK_SIZE / OWFS_CATALOG_ENTRY_SIZE); ++e) {
            owfs_catalog_entry_t *centry = (owfs_catalog_entry_t *)(block_buf + (e * OWFS_CATALOG_ENTRY_SIZE));
            if (centry->entry_type != 0 && !(centry->entry_type & OWFS_ENTRY_DELETED)) {
                if (ow_sutf8_name_cmp(centry->name, centry->name_length, name, name_len) == 0) {
                    centry->entry_type |= OWFS_ENTRY_DELETED;
                    centry->checksum = owfs_catalog_entry_compute_checksum(centry);
                    hres = htl_write_block(dev, bnum, block_buf);
                    if (hres != HTL_OK) return OWFS_ERR_IO;
                    return OWFS_OK;
                }
            }
        }
    }
    return OWFS_ERR_NOT_FOUND;
}

owfs_status_t owfs_catalog_list(htl_device_t *dev, const owfs_superblock_t *sb, uint32_t catalog_inode, owfs_catalog_entry_t *entries, uint32_t max_entries, uint32_t *out_count) {
    if (!dev || !sb || !entries || !out_count || max_entries == 0) {
        return OWFS_ERR_INVALID_PARAM;
    }
    *out_count = 0;

    owfs_inode_t cinode;
    owfs_status_t res = owfs_inode_read(dev, sb, catalog_inode, &cinode);
    if (res != OWFS_OK) return res;

    static uint8_t block_buf[OWFS_BLOCK_SIZE];

    for (size_t b = 0; b < OWFS_DIRECT_BLOCKS; ++b) {
        uint32_t bnum = cinode.direct_blocks[b];
        if (bnum == 0) continue;

        htl_status_t hres = htl_read_block(dev, bnum, block_buf);
        if (hres != HTL_OK) return OWFS_ERR_IO;

        for (size_t e = 0; e < (OWFS_BLOCK_SIZE / OWFS_CATALOG_ENTRY_SIZE); ++e) {
            const owfs_catalog_entry_t *centry = (const owfs_catalog_entry_t *)(block_buf + (e * OWFS_CATALOG_ENTRY_SIZE));
            if (centry->entry_type != 0 && !(centry->entry_type & OWFS_ENTRY_DELETED)) {
                if (owfs_catalog_entry_verify_checksum(centry)) {
                    if (*out_count < max_entries) {
                        ow_memcpy(&entries[*out_count], centry, sizeof(owfs_catalog_entry_t));
                        (*out_count)++;
                    }
                }
            }
        }
    }
    return OWFS_OK;
}
