#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "../include/owfs_file.h"
#include "../include/owfs_blockmap.h"
#include "../include/owfs_bitmap.h"
#include "../include/owfs_sync.h"
#include "../../common/include/ow_crypto.h"
#include "../../common/include/ow_mem.h"
#include "../../common/include/ow_htl.h"
#include "../../common/include/ow_sec.h"

static bool is_encrypted(const owfs_superblock_t *sb, const owfs_inode_t *inode) {
    return (sb->security_flags & OWFS_SEC_ENCRYPTED) &&
           (inode->security_flags & OWFS_SEC_ENCRYPTED);
}

static void crypto_xform(const owfs_superblock_t *sb, const owfs_inode_t *inode,
                         uint8_t *buf, uint32_t block_num) {
    if (is_encrypted(sb, inode)) {
        ow_chacha20_xor(block_num, sb->key_slot_1, sb->crypto_nonce, buf, OWFS_BLOCK_SIZE);
    }
}

static owfs_status_t ensure_blocks(htl_device_t *dev, owfs_superblock_t *sb,
                                   owfs_inode_t *inode, uint32_t last_block) {
    while (inode->block_count <= last_block) {
        uint32_t b = 0;
        owfs_status_t res = owfs_blockmap_ensure(dev, sb, inode, inode->block_count, &b);
        if (res != OWFS_OK) {
            return res;
        }
    }
    return OWFS_OK;
}

owfs_status_t owfs_file_read(htl_device_t *dev, const owfs_superblock_t *sb,
                             const owfs_inode_t *inode,
                             uint8_t *buf, uint32_t offset, uint32_t len,
                             uint32_t *out_read) {
    if (!dev || !sb || !inode || !buf || !out_read) {
        return OWFS_ERR_INVALID_PARAM;
    }
    *out_read = 0;
    if (!(inode->entry_type & OWFS_ENTRY_FILE)) {
        return OWFS_ERR_NOT_FILE;
    }
    owfs_status_t access = owfs_inode_access_check(inode, OW_ACCESS_READ);
    if (access != OWFS_OK) {
        return access;
    }
    if (offset >= inode->size_bytes || len == 0) {
        return OWFS_OK;
    }
    if (len > (inode->size_bytes - offset)) {
        len = inode->size_bytes - offset;
    }

    static uint8_t block_buf[OWFS_BLOCK_SIZE];
    uint32_t done = 0;
    while (done < len) {
        uint32_t pos = offset + done;
        uint32_t idx = pos >> OWFS_BLOCK_SHIFT;
        uint32_t off = pos & (OWFS_BLOCK_SIZE - 1);
        uint32_t chunk = len - done;
        if (chunk > (OWFS_BLOCK_SIZE - off)) {
            chunk = OWFS_BLOCK_SIZE - off;
        }

        uint32_t bnum = 0;
        owfs_status_t res = owfs_blockmap_get(dev, inode, idx, &bnum);
        if (res != OWFS_OK) {
            return res;
        }
        htl_status_t hres = htl_read_block(dev, bnum, block_buf);
        if (hres != HTL_OK) {
            return OWFS_ERR_IO;
        }
        crypto_xform(sb, inode, block_buf, bnum);
        ow_memcpy(buf + done, block_buf + off, chunk);
        done += chunk;
    }
    *out_read = done;
    return OWFS_OK;
}

owfs_status_t owfs_file_write(htl_device_t *dev, owfs_superblock_t *sb,
                              owfs_inode_t *inode, uint32_t inode_num,
                              const uint8_t *buf, uint32_t offset, uint32_t len,
                              uint32_t *out_written) {
    if (!dev || !sb || !inode || !buf || !out_written) {
        return OWFS_ERR_INVALID_PARAM;
    }
    *out_written = 0;
    if (!(inode->entry_type & OWFS_ENTRY_FILE)) {
        return OWFS_ERR_NOT_FILE;
    }
    if (len == 0) {
        return OWFS_OK;
    }
    owfs_status_t vw = owfs_volume_writable(sb);
    if (vw != OWFS_OK) {
        return vw;
    }
    if (inode->security_flags & OWFS_SEC_READONLY) {
        return OWFS_ERR_WRITE_PROTECTED;
    }
    owfs_status_t access = owfs_inode_access_check(inode, OW_ACCESS_WRITE);
    if (access != OWFS_OK) {
        return access;
    }
    if ((uint64_t)offset + len > (uint64_t)OWFS_MAX_LOGICAL_BLOCKS * OWFS_BLOCK_SIZE) {
        return OWFS_ERR_BUFFER_TOO_SMALL;
    }
    if (inode_num >= sb->total_inodes) {
        return OWFS_ERR_NOT_FOUND;
    }

    uint32_t end = offset + len;
    uint32_t last_block = (end - 1) >> OWFS_BLOCK_SHIFT;

    owfs_status_t res = ensure_blocks(dev, sb, inode, last_block);
    if (res != OWFS_OK) {
        return res;
    }

    static uint8_t block_buf[OWFS_BLOCK_SIZE];
    uint32_t written = 0;
    uint32_t failure = OWFS_OK;

    while (written < len) {
        uint32_t pos = offset + written;
        uint32_t idx = pos >> OWFS_BLOCK_SHIFT;
        uint32_t off = pos & (OWFS_BLOCK_SIZE - 1);
        uint32_t chunk = len - written;
        if (chunk > (OWFS_BLOCK_SIZE - off)) {
            chunk = OWFS_BLOCK_SIZE - off;
        }

        uint32_t bnum = 0;
        res = owfs_blockmap_get(dev, inode, idx, &bnum);
        if (res != OWFS_OK) {
            failure = res;
            break;
        }

        htl_status_t hres;
        if (off == 0 && chunk == OWFS_BLOCK_SIZE) {
            ow_memcpy(block_buf, buf + written, OWFS_BLOCK_SIZE);
            crypto_xform(sb, inode, block_buf, bnum);
        } else {
            hres = htl_read_block(dev, bnum, block_buf);
            if (hres != HTL_OK) {
                failure = OWFS_ERR_IO;
                break;
            }
            crypto_xform(sb, inode, block_buf, bnum);
            ow_memcpy(block_buf + off, buf + written, chunk);
            crypto_xform(sb, inode, block_buf, bnum);
        }

        hres = htl_write_block(dev, bnum, block_buf);
        if (hres != HTL_OK) {
            failure = (hres == HTL_ERR_WRITE_PROTECT) ? OWFS_ERR_WRITE_PROTECTED : OWFS_ERR_IO;
            break;
        }
        written += chunk;
    }

    uint32_t new_size = offset + written;
    if (new_size > inode->size_bytes) {
        inode->size_bytes = new_size;
    }

    res = owfs_inode_write(dev, sb, inode_num, inode);
    if (res != OWFS_OK) {
        return res;
    }

    *out_written = written;
    if (failure != OWFS_OK && written == 0) {
        return failure;
    }
    return OWFS_OK;
}

owfs_status_t owfs_file_truncate(htl_device_t *dev, owfs_superblock_t *sb,
                                 owfs_inode_t *inode, uint32_t inode_num,
                                 uint32_t new_size) {
    if (!dev || !sb || !inode) {
        return OWFS_ERR_INVALID_PARAM;
    }
    if (!(inode->entry_type & OWFS_ENTRY_FILE)) {
        return OWFS_ERR_NOT_FILE;
    }
    owfs_status_t vw = owfs_volume_writable(sb);
    if (vw != OWFS_OK) {
        return vw;
    }
    if (inode->security_flags & OWFS_SEC_READONLY) {
        return OWFS_ERR_WRITE_PROTECTED;
    }
    owfs_status_t access = owfs_inode_access_check(inode, OW_ACCESS_WRITE);
    if (access != OWFS_OK) {
        return access;
    }
    if (new_size >= inode->size_bytes) {
        return OWFS_OK;
    }
    if (inode_num >= sb->total_inodes) {
        return OWFS_ERR_NOT_FOUND;
    }

    uint32_t new_blocks = new_size >> OWFS_BLOCK_SHIFT;
    if ((new_size & (OWFS_BLOCK_SIZE - 1)) != 0) {
        new_blocks++;
    }
    if (new_blocks < inode->block_count) {
        owfs_status_t res = owfs_blockmap_release(dev, sb, inode, new_blocks);
        if (res != OWFS_OK) {
            return res;
        }
    }

    inode->size_bytes = new_size;
    return owfs_inode_write(dev, sb, inode_num, inode);
}
