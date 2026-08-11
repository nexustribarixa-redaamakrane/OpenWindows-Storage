#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "../include/owfs_catalog.h"
#include "../include/owfs_bitmap.h"
#include "../include/owfs_blockmap.h"
#include "../include/owfs_sync.h"
#include "../../common/include/ow_checksum.h"
#include "../../common/include/ow_mem.h"
#include "../../common/include/ow_sec.h"
#include "../../common/include/ow_string.h"

/* A hidden inode is invisible to all but its owner and the superuser. */
static bool inode_hidden_from(htl_device_t *dev, const owfs_superblock_t *sb,
                              uint32_t inode_num) {
    owfs_inode_t ino;
    if (owfs_inode_read(dev, sb, inode_num, &ino) != OWFS_OK) {
        return false;
    }
    return owfs_inode_is_hidden(&ino) && !ow_sec_is_superuser() &&
           ow_sec_current().uid != ino.owner_uid;
}

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

static owfs_status_t catalog_read(htl_device_t *dev, const owfs_superblock_t *sb,
                                  uint32_t catalog_inode, owfs_inode_t *cinode) {
    if (!dev || !sb || !cinode) {
        return OWFS_ERR_INVALID_PARAM;
    }
    owfs_status_t res = owfs_inode_read(dev, sb, catalog_inode, cinode);
    if (res != OWFS_OK) {
        return res;
    }
    if (!(cinode->entry_type & OWFS_ENTRY_CATALOG)) {
        return OWFS_ERR_NOT_CATALOG;
    }
    return OWFS_OK;
}

owfs_status_t owfs_catalog_lookup(htl_device_t *dev, const owfs_superblock_t *sb, uint32_t catalog_inode, const uint8_t *name, size_t name_len, uint32_t *out_inode) {
    if (!dev || !sb || !name || !out_inode || name_len == 0) {
        return OWFS_ERR_INVALID_PARAM;
    }
    if (name_len >= OWFS_NAME_MAX_BYTES) {
        return OWFS_ERR_NAME_TOO_LONG;
    }

    owfs_inode_t cinode;
    owfs_status_t res = catalog_read(dev, sb, catalog_inode, &cinode);
    if (res != OWFS_OK) {
        return res;
    }
    res = owfs_inode_access_check(&cinode, OW_ACCESS_READ);
    if (res != OWFS_OK) {
        return res;
    }

    static uint8_t block_buf[OWFS_BLOCK_SIZE];

    for (uint32_t idx = 0; idx < cinode.block_count; ++idx) {
        uint32_t bnum = 0;
        res = owfs_blockmap_get(dev, &cinode, idx, &bnum);
        if (res != OWFS_OK) {
            return res;
        }

        htl_status_t hres = htl_read_block(dev, bnum, block_buf);
        if (hres != HTL_OK) return OWFS_ERR_IO;

        for (size_t e = 0; e < OWFS_ENTRIES_PER_BLOCK; ++e) {
            const owfs_catalog_entry_t *centry = (const owfs_catalog_entry_t *)(block_buf + (e * OWFS_CATALOG_ENTRY_SIZE));
            if (centry->entry_type != 0 && !(centry->entry_type & OWFS_ENTRY_DELETED)) {
                if (owfs_catalog_entry_verify_checksum(centry)) {
                    if (ow_sutf8_name_cmp(centry->name, centry->name_length, name, name_len) == 0) {
                        if (inode_hidden_from(dev, sb, centry->inode_number)) {
                            continue;
                        }
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
    if (entry_type != OWFS_ENTRY_FILE && entry_type != OWFS_ENTRY_CATALOG) {
        return OWFS_ERR_INVALID_PARAM;
    }
    if (!ow_sutf8_validate(name, name_len)) {
        return OWFS_ERR_INVALID_PARAM;
    }
    owfs_status_t vw = owfs_volume_writable(sb);
    if (vw != OWFS_OK) {
        return vw;
    }

    uint32_t existing = 0;
    if (owfs_catalog_lookup(dev, sb, catalog_inode, name, name_len, &existing) == OWFS_OK) {
        return OWFS_ERR_ALREADY_EXISTS;
    }

    owfs_inode_t cinode;
    owfs_status_t res = catalog_read(dev, sb, catalog_inode, &cinode);
    if (res != OWFS_OK) {
        return res;
    }
    res = owfs_inode_access_check(&cinode, OW_ACCESS_WRITE);
    if (res != OWFS_OK) {
        return res;
    }

    static uint8_t block_buf[OWFS_BLOCK_SIZE];

    for (uint32_t idx = 0; idx < cinode.block_count; ++idx) {
        uint32_t bnum = 0;
        res = owfs_blockmap_get(dev, &cinode, idx, &bnum);
        if (res != OWFS_OK) {
            return res;
        }

        htl_status_t hres = htl_read_block(dev, bnum, block_buf);
        if (hres != HTL_OK) return OWFS_ERR_IO;

        for (size_t e = 0; e < OWFS_ENTRIES_PER_BLOCK; ++e) {
            owfs_catalog_entry_t *centry = (owfs_catalog_entry_t *)(block_buf + (e * OWFS_CATALOG_ENTRY_SIZE));
            if (centry->entry_type == 0 || (centry->entry_type & OWFS_ENTRY_DELETED)) {
                ow_memset(centry, 0, sizeof(owfs_catalog_entry_t));
                centry->inode_number = target_inode;
                centry->entry_type = entry_type;
                centry->name_length = (uint8_t)name_len;
                ow_memcpy(centry->name, name, name_len);
                centry->checksum = owfs_catalog_entry_compute_checksum(centry);

                hres = htl_write_block(dev, bnum, block_buf);
                if (hres != HTL_OK) {
                    return (hres == HTL_ERR_WRITE_PROTECT) ? OWFS_ERR_WRITE_PROTECTED : OWFS_ERR_IO;
                }
                return OWFS_OK;
            }
        }
    }

    /* No free slot in existing blocks: grow the catalog by one block. */
    uint32_t bnum = 0;
    res = owfs_blockmap_ensure(dev, sb, &cinode, cinode.block_count, &bnum);
    if (res != OWFS_OK) {
        return (res == OWFS_ERR_NO_FREE_BLOCKS) ? OWFS_ERR_CATALOG_FULL : res;
    }

    ow_memset(block_buf, 0, sizeof(block_buf));
    owfs_catalog_entry_t *centry = (owfs_catalog_entry_t *)block_buf;
    centry->inode_number = target_inode;
    centry->entry_type = entry_type;
    centry->name_length = (uint8_t)name_len;
    ow_memcpy(centry->name, name, name_len);
    centry->checksum = owfs_catalog_entry_compute_checksum(centry);

    htl_status_t hres = htl_write_block(dev, bnum, block_buf);
    if (hres != HTL_OK) {
        return (hres == HTL_ERR_WRITE_PROTECT) ? OWFS_ERR_WRITE_PROTECTED : OWFS_ERR_IO;
    }
    return owfs_inode_write(dev, sb, catalog_inode, &cinode);
}

owfs_status_t owfs_catalog_remove(htl_device_t *dev, owfs_superblock_t *sb, uint32_t catalog_inode, const uint8_t *name, size_t name_len) {
    if (!dev || !sb || !name || name_len == 0) {
        return OWFS_ERR_INVALID_PARAM;
    }
    if (name_len >= OWFS_NAME_MAX_BYTES) {
        return OWFS_ERR_NAME_TOO_LONG;
    }
    owfs_status_t vw = owfs_volume_writable(sb);
    if (vw != OWFS_OK) {
        return vw;
    }

    owfs_inode_t cinode;
    owfs_status_t res = catalog_read(dev, sb, catalog_inode, &cinode);
    if (res != OWFS_OK) {
        return res;
    }
    res = owfs_inode_access_check(&cinode, OW_ACCESS_WRITE);
    if (res != OWFS_OK) {
        return res;
    }

    static uint8_t block_buf[OWFS_BLOCK_SIZE];

    for (uint32_t idx = 0; idx < cinode.block_count; ++idx) {
        uint32_t bnum = 0;
        res = owfs_blockmap_get(dev, &cinode, idx, &bnum);
        if (res != OWFS_OK) {
            return res;
        }

        htl_status_t hres = htl_read_block(dev, bnum, block_buf);
        if (hres != HTL_OK) return OWFS_ERR_IO;

        for (size_t e = 0; e < OWFS_ENTRIES_PER_BLOCK; ++e) {
            owfs_catalog_entry_t *centry = (owfs_catalog_entry_t *)(block_buf + (e * OWFS_CATALOG_ENTRY_SIZE));
            if (centry->entry_type != 0 && !(centry->entry_type & OWFS_ENTRY_DELETED)) {
                if (ow_sutf8_name_cmp(centry->name, centry->name_length, name, name_len) == 0) {
                    if (inode_hidden_from(dev, sb, centry->inode_number)) {
                        return OWFS_ERR_NOT_FOUND;
                    }
                    centry->entry_type |= OWFS_ENTRY_DELETED;
                    centry->checksum = owfs_catalog_entry_compute_checksum(centry);
                    hres = htl_write_block(dev, bnum, block_buf);
                    if (hres != HTL_OK) {
                        return (hres == HTL_ERR_WRITE_PROTECT) ? OWFS_ERR_WRITE_PROTECTED : OWFS_ERR_IO;
                    }
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
    owfs_status_t res = catalog_read(dev, sb, catalog_inode, &cinode);
    if (res != OWFS_OK) {
        return res;
    }
    res = owfs_inode_access_check(&cinode, OW_ACCESS_READ);
    if (res != OWFS_OK) {
        return res;
    }

    static uint8_t block_buf[OWFS_BLOCK_SIZE];

    for (uint32_t idx = 0; idx < cinode.block_count; ++idx) {
        uint32_t bnum = 0;
        res = owfs_blockmap_get(dev, &cinode, idx, &bnum);
        if (res != OWFS_OK) {
            return res;
        }

        htl_status_t hres = htl_read_block(dev, bnum, block_buf);
        if (hres != HTL_OK) return OWFS_ERR_IO;

        for (size_t e = 0; e < OWFS_ENTRIES_PER_BLOCK; ++e) {
            const owfs_catalog_entry_t *centry = (const owfs_catalog_entry_t *)(block_buf + (e * OWFS_CATALOG_ENTRY_SIZE));
            if (centry->entry_type != 0 && !(centry->entry_type & OWFS_ENTRY_DELETED)) {
                if (owfs_catalog_entry_verify_checksum(centry)) {
                    if (inode_hidden_from(dev, sb, centry->inode_number)) {
                        continue;
                    }
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

owfs_status_t owfs_catalog_create(htl_device_t *dev, owfs_superblock_t *sb,
                                  uint32_t parent_inode,
                                  const uint8_t *name, size_t name_len,
                                  uint32_t *out_inode) {
    if (!dev || !sb || !out_inode) {
        return OWFS_ERR_INVALID_PARAM;
    }
    uint32_t ino = 0;
    owfs_status_t res = owfs_inode_alloc(dev, sb, OWFS_ENTRY_FILE, name, name_len, parent_inode, &ino);
    if (res != OWFS_OK) {
        return res;
    }
    res = owfs_catalog_insert(dev, sb, parent_inode, name, name_len, ino, OWFS_ENTRY_FILE);
    if (res != OWFS_OK) {
        owfs_inode_free(dev, sb, ino);
        return res;
    }
    *out_inode = ino;
    return OWFS_OK;
}

owfs_status_t owfs_catalog_mkdir(htl_device_t *dev, owfs_superblock_t *sb,
                                 uint32_t parent_inode,
                                 const uint8_t *name, size_t name_len,
                                 uint32_t *out_inode) {
    if (!dev || !sb || !out_inode) {
        return OWFS_ERR_INVALID_PARAM;
    }
    uint32_t ino = 0;
    owfs_status_t res = owfs_inode_alloc(dev, sb, OWFS_ENTRY_CATALOG, name, name_len, parent_inode, &ino);
    if (res != OWFS_OK) {
        return res;
    }
    res = owfs_catalog_insert(dev, sb, parent_inode, name, name_len, ino, OWFS_ENTRY_CATALOG);
    if (res != OWFS_OK) {
        owfs_inode_free(dev, sb, ino);
        return res;
    }
    *out_inode = ino;
    return OWFS_OK;
}
