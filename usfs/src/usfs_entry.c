#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "../include/usfs_entry.h"
#include "../include/usfs_bitmap.h"
#include "../include/usfs_sync.h"
#include "../../common/include/ow_checksum.h"
#include "../../common/include/ow_mem.h"
#include "../../common/include/ow_sec.h"
#include "../../common/include/ow_string.h"

static bool entry_is_hidden(const usfs_entry_t *entry) {
    return (entry->security_flags & USFS_SEC_HIDDEN) != 0;
}

/* Non-superuser callers must hold the requested access to `entry`. */
static usfs_status_t entry_check_access(const usfs_entry_t *entry, uint8_t want) {
    if (!entry) {
        return USFS_ERR_INVALID_PARAM;
    }
    if (entry->entry_type == 0) {
        return USFS_ERR_NOT_FOUND;
    }
    if (!ow_sec_access(entry->permissions, entry->owner_uid, entry->owner_gid, want)) {
        return USFS_ERR_ACCESS_DENIED;
    }
    return USFS_OK;
}

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

usfs_status_t usfs_entry_write(htl_device_t *dev, usfs_superblock_t *sb, uint32_t entry_idx, usfs_entry_t *entry) {
    if (!dev || !sb || !entry) {
        return USFS_ERR_INVALID_PARAM;
    }
    if (entry_idx >= sb->total_entries) {
        return USFS_ERR_NOT_FOUND;
    }
    usfs_status_t vw = usfs_volume_writable(sb);
    if (vw != USFS_OK) {
        return vw;
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

static usfs_status_t default_mode_for(uint8_t entry_type, uint16_t *out_mode) {
    if (entry_type == USFS_ENTRY_CATALOG) {
        *out_mode = USFS_MODE_DEFAULT_DIR;
        return USFS_OK;
    }
    if (entry_type == USFS_ENTRY_FILE) {
        *out_mode = USFS_MODE_DEFAULT_FILE;
        return USFS_OK;
    }
    return USFS_ERR_INVALID_PARAM;
}

usfs_status_t usfs_entry_alloc(htl_device_t *dev, usfs_superblock_t *sb,
                               uint8_t entry_type,
                               const uint8_t *name, size_t name_len,
                               uint32_t parent, uint32_t *out_entry_idx) {
    if (!dev || !sb || !name || !out_entry_idx) {
        return USFS_ERR_INVALID_PARAM;
    }
    if (entry_type != USFS_ENTRY_FILE && entry_type != USFS_ENTRY_CATALOG) {
        return USFS_ERR_INVALID_PARAM;
    }
    if (name_len == 0 || name_len >= USFS_NAME_MAX_BYTES) {
        return USFS_ERR_NAME_TOO_LONG;
    }
    if (!ow_sutf8_validate(name, name_len)) {
        return USFS_ERR_INVALID_PARAM;
    }
    if (sb->used_entries >= sb->total_entries) {
        return USFS_ERR_NO_FREE_ENTRIES;
    }
    usfs_status_t vw = usfs_volume_writable(sb);
    if (vw != USFS_OK) {
        return vw;
    }

    /* Creating an entry requires write access to the parent catalog. The
     * bootstrap root creation (parent 0 before the root entry exists) is
     * exempt. */
    if (parent != 0 || sb->used_entries > 0) {
        usfs_entry_t parent_entry;
        usfs_status_t pres = usfs_entry_read(dev, sb, parent, &parent_entry);
        if (pres != USFS_OK) {
            return pres;
        }
        if (parent_entry.entry_type == 0) {
            return USFS_ERR_NOT_FOUND;
        }
        pres = entry_check_access(&parent_entry, OW_ACCESS_WRITE);
        if (pres != USFS_OK) {
            return pres;
        }
    }

    uint16_t mode = 0;
    usfs_status_t res = default_mode_for(entry_type, &mode);
    if (res != USFS_OK) {
        return res;
    }

    usfs_entry_t temp;
    uint32_t chosen = 0;
    bool found = false;
    for (uint32_t i = 0; i < sb->total_entries; ++i) {
        res = usfs_entry_read(dev, sb, i, &temp);
        if (res == USFS_OK) {
            if (temp.entry_type == 0 || (temp.entry_type & USFS_ENTRY_DELETED)) {
                chosen = i;
                found = true;
                break;
            }
        } else if (res != USFS_ERR_CHECKSUM_MISMATCH) {
            return res;
        }
    }
    if (!found) {
        return USFS_ERR_NO_FREE_ENTRIES;
    }

    ow_memset(&temp, 0, sizeof(usfs_entry_t));
    ow_identity_t id = ow_sec_current();
    temp.entry_index = chosen;
    temp.entry_type = entry_type;
    temp.permissions = mode;
    temp.owner_uid = (uint16_t)id.uid;
    temp.owner_gid = (uint16_t)id.gid;
    temp.parent_entry = parent;
    temp.name_length = (uint8_t)name_len;
    temp.security_flags = (sb->security_flags & USFS_SEC_ENCRYPTED) ? USFS_SEC_ENCRYPTED : 0;
    ow_sutf8_name_copy(temp.name, USFS_NAME_MAX_BYTES, name, name_len);

    if (entry_type == USFS_ENTRY_CATALOG) {
        uint32_t first = 0;
        res = usfs_block_alloc(dev, sb, 1, &first);
        if (res != USFS_OK) {
            return res;
        }
        static uint8_t zero_block[USFS_BLOCK_SIZE];
        ow_memset(zero_block, 0, sizeof(zero_block));
        htl_status_t hres = htl_write_block(dev, first, zero_block);
        if (hres != HTL_OK) {
            usfs_block_free(dev, sb, first, 1);
            return (hres == HTL_ERR_WRITE_PROTECT) ? USFS_ERR_WRITE_PROTECTED : USFS_ERR_IO;
        }
        temp.first_block = first;
        temp.block_count = 1;
    }

    res = usfs_entry_write(dev, sb, chosen, &temp);
    if (res != USFS_OK) {
        if (temp.block_count > 0) {
            usfs_block_free(dev, sb, temp.first_block, temp.block_count);
        }
        return res;
    }

    sb->used_entries++;
    *out_entry_idx = chosen;
    return USFS_OK;
}

usfs_status_t usfs_entry_free(htl_device_t *dev, usfs_superblock_t *sb, uint32_t entry_idx) {
    if (!dev || !sb) {
        return USFS_ERR_INVALID_PARAM;
    }
    usfs_status_t vw = usfs_volume_writable(sb);
    if (vw != USFS_OK) {
        return vw;
    }
    usfs_entry_t temp;
    usfs_status_t res = usfs_entry_read(dev, sb, entry_idx, &temp);
    if (res != USFS_OK) {
        return res;
    }
    if (temp.entry_type == 0) {
        return USFS_ERR_NOT_FOUND;
    }
    if (temp.security_flags & USFS_SEC_READONLY) {
        return USFS_ERR_WRITE_PROTECTED;
    }
    res = entry_check_access(&temp, OW_ACCESS_WRITE);
    if (res != USFS_OK) {
        return res;
    }

    if (temp.block_count > 0) {
        res = usfs_block_free(dev, sb, temp.first_block, temp.block_count);
        if (res != USFS_OK) {
            return res;
        }
        temp.first_block = 0;
        temp.block_count = 0;
        temp.size_bytes = 0;
    }

    temp.entry_type = USFS_ENTRY_DELETED;
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
                    if (entry_is_hidden(&temp) && !ow_sec_is_superuser() &&
                        ow_sec_current().uid != temp.owner_uid) {
                        continue; /* hidden from all but owner / superuser */
                    }
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
