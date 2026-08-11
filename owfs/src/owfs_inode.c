#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "../include/owfs_inode.h"
#include "../include/owfs_blockmap.h"
#include "../include/owfs_sync.h"
#include "../../common/include/ow_checksum.h"
#include "../../common/include/ow_mem.h"
#include "../../common/include/ow_sec.h"
#include "../../common/include/ow_string.h"

bool owfs_inode_is_hidden(const owfs_inode_t *inode) {
    if (!inode) {
        return false;
    }
    return (inode->security_flags & OWFS_SEC_HIDDEN) != 0;
}

owfs_status_t owfs_inode_access_check(const owfs_inode_t *inode, uint8_t want) {
    if (!inode) {
        return OWFS_ERR_INVALID_PARAM;
    }
    if (!ow_sec_access(inode->permissions, inode->owner_uid, inode->owner_gid, want)) {
        return OWFS_ERR_ACCESS_DENIED;
    }
    return OWFS_OK;
}

uint32_t owfs_inode_compute_checksum(const owfs_inode_t *inode) {
    if (!inode) {
        return 0;
    }
    return ow_crc32c_struct(inode, sizeof(owfs_inode_t), offsetof(owfs_inode_t, checksum));
}

bool owfs_inode_verify_checksum(const owfs_inode_t *inode) {
    if (!inode) {
        return false;
    }
    return inode->checksum == owfs_inode_compute_checksum(inode);
}

static uint32_t inode_block_offset(const owfs_superblock_t *sb, uint32_t inode_num) {
    uint32_t block_idx = inode_num >> 4;
    return sb->inode_table_start + block_idx;
}

static uint32_t inode_entry_index(uint32_t inode_num) {
    return inode_num & 0x0F;
}

owfs_status_t owfs_inode_read(htl_device_t *dev, const owfs_superblock_t *sb, uint32_t inode_num, owfs_inode_t *inode) {
    if (!dev || !sb || !inode) {
        return OWFS_ERR_INVALID_PARAM;
    }
    if (inode_num >= sb->total_inodes) {
        return OWFS_ERR_NOT_FOUND;
    }

    static uint8_t block_buf[OWFS_BLOCK_SIZE];
    uint32_t block_num = inode_block_offset(sb, inode_num);
    htl_status_t hres = htl_read_block(dev, block_num, block_buf);
    if (hres != HTL_OK) {
        return OWFS_ERR_IO;
    }

    uint32_t idx = inode_entry_index(inode_num);
    const owfs_inode_t *src = (const owfs_inode_t *)(block_buf + (idx * OWFS_INODE_SIZE));
    ow_memcpy(inode, src, sizeof(owfs_inode_t));

    if (inode->entry_type == 0) {
        return OWFS_OK;
    }
    if (!owfs_inode_verify_checksum(inode)) {
        return OWFS_ERR_CHECKSUM_MISMATCH;
    }
    return OWFS_OK;
}

owfs_status_t owfs_inode_write(htl_device_t *dev, const owfs_superblock_t *sb, uint32_t inode_num, owfs_inode_t *inode) {
    if (!dev || !sb || !inode) {
        return OWFS_ERR_INVALID_PARAM;
    }
    if (inode_num >= sb->total_inodes) {
        return OWFS_ERR_NOT_FOUND;
    }
    owfs_status_t vw = owfs_volume_writable(sb);
    if (vw != OWFS_OK) {
        return vw;
    }

    static uint8_t block_buf[OWFS_BLOCK_SIZE];
    uint32_t block_num = inode_block_offset(sb, inode_num);
    htl_status_t hres = htl_read_block(dev, block_num, block_buf);
    if (hres != HTL_OK) {
        return OWFS_ERR_IO;
    }

    inode->checksum = owfs_inode_compute_checksum(inode);
    uint32_t idx = inode_entry_index(inode_num);
    owfs_inode_t *dst = (owfs_inode_t *)(block_buf + (idx * OWFS_INODE_SIZE));
    ow_memcpy(dst, inode, sizeof(owfs_inode_t));

    hres = htl_write_block(dev, block_num, block_buf);
    if (hres != HTL_OK) {
        if (hres == HTL_ERR_WRITE_PROTECT) {
            return OWFS_ERR_WRITE_PROTECTED;
        }
        return OWFS_ERR_IO;
    }
    return OWFS_OK;
}

owfs_status_t owfs_inode_alloc(htl_device_t *dev, owfs_superblock_t *sb,
                               uint8_t entry_type,
                               const uint8_t *name, size_t name_len,
                               uint32_t parent, uint32_t *out_inode_num) {
    if (!dev || !sb || !out_inode_num) {
        return OWFS_ERR_INVALID_PARAM;
    }
    if (entry_type != OWFS_ENTRY_FILE && entry_type != OWFS_ENTRY_CATALOG) {
        return OWFS_ERR_INVALID_PARAM;
    }
    if (name_len >= OWFS_NAME_MAX_BYTES) {
        return OWFS_ERR_NAME_TOO_LONG;
    }
    if (name && name_len > 0 && !ow_sutf8_validate(name, name_len)) {
        return OWFS_ERR_INVALID_PARAM;
    }
    owfs_status_t vw = owfs_volume_writable(sb);
    if (vw != OWFS_OK) {
        return vw;
    }
    if (sb->free_inodes == 0) {
        return OWFS_ERR_NO_FREE_INODES;
    }

    owfs_inode_t temp;
    for (uint32_t i = 0; i < sb->total_inodes; ++i) {
        owfs_status_t res = owfs_inode_read(dev, sb, i, &temp);
        if (res != OWFS_OK && res != OWFS_ERR_CHECKSUM_MISMATCH) {
            return res;
        }
        if (temp.entry_type == 0 || (temp.entry_type & OWFS_ENTRY_DELETED)) {
            ow_identity_t id = ow_sec_current();
            ow_memset(&temp, 0, sizeof(owfs_inode_t));
            temp.inode_number = i;
            temp.entry_type = entry_type;
            temp.permissions = (entry_type == OWFS_ENTRY_CATALOG) ? OWFS_MODE_DEFAULT_DIR : OWFS_MODE_DEFAULT_FILE;
            temp.owner_uid = (uint16_t)id.uid;
            temp.owner_gid = (uint16_t)id.gid;
            temp.security_flags = (sb->security_flags & OWFS_SEC_ENCRYPTED) ? OWFS_SEC_ENCRYPTED : 0;
            temp.parent_inode = parent;
            temp.name_length = (uint8_t)name_len;
            if (name && name_len > 0) {
                ow_memcpy(temp.name, name, name_len);
            }
            temp.checksum = owfs_inode_compute_checksum(&temp);

            res = owfs_inode_write(dev, sb, i, &temp);
            if (res != OWFS_OK) {
                return res;
            }
            sb->free_inodes--;
            *out_inode_num = i;
            return OWFS_OK;
        }
    }
    return OWFS_ERR_NO_FREE_INODES;
}

owfs_status_t owfs_inode_free(htl_device_t *dev, owfs_superblock_t *sb, uint32_t inode_num) {
    if (!dev || !sb) {
        return OWFS_ERR_INVALID_PARAM;
    }
    owfs_status_t vw = owfs_volume_writable(sb);
    if (vw != OWFS_OK) {
        return vw;
    }
    owfs_inode_t temp;
    owfs_status_t res = owfs_inode_read(dev, sb, inode_num, &temp);
    if (res != OWFS_OK) {
        return res;
    }
    if (temp.entry_type == 0) {
        return OWFS_ERR_NOT_FOUND;
    }
    if (temp.entry_type & OWFS_ENTRY_DELETED) {
        return OWFS_OK;
    }
    if (temp.security_flags & OWFS_SEC_READONLY) {
        return OWFS_ERR_WRITE_PROTECTED;
    }
    owfs_status_t access = owfs_inode_access_check(&temp, OW_ACCESS_WRITE);
    if (access != OWFS_OK) {
        return access;
    }

    res = owfs_blockmap_release(dev, sb, &temp, 0);
    if (res != OWFS_OK) {
        return res;
    }

    temp.entry_type |= OWFS_ENTRY_DELETED;
    temp.size_bytes = 0;
    temp.block_count = 0;
    res = owfs_inode_write(dev, sb, inode_num, &temp);
    if (res == OWFS_OK) {
        sb->free_inodes++;
    }
    return res;
}
