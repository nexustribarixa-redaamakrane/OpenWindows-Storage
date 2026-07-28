#ifndef OWFS_TYPES_H
#define OWFS_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "../../common/include/ow_htl.h"

#define OWFS_BLOCK_SIZE         0x1000      /* 4096 bytes */
#define OWFS_BLOCK_SHIFT        12          /* log2(0x1000) */
#define OWFS_PARTITION_OFFSET   0x10000UL   /* 64 KiB reserved for MBL */
#define OWFS_INODE_SIZE         0x100       /* 256 bytes per inode */
#define OWFS_CATALOG_ENTRY_SIZE 0x100       /* 256 bytes per catalog entry */
#define OWFS_NAME_MAX_BYTES     128         /* SUTF-8 encoded name field */
#define OWFS_MAGIC              0x4F574653UL /* 'OWFS' */
#define OWFS_VERSION_MAJOR      1
#define OWFS_VERSION_MINOR      0
#define OWFS_ROOT_INODE         0           /* Inode 0 = root catalog ('/') */

/* Entry type flags */
#define OWFS_ENTRY_FILE         0x01
#define OWFS_ENTRY_CATALOG      0x02        /* Directory at disk level */
#define OWFS_ENTRY_DELETED      0x80

/* Volume state flags (power-cut protection) */
#define OWFS_STATE_CLEAN        0x0000
#define OWFS_STATE_DIRTY        0x0001      /* Volume was not cleanly unmounted */
#define OWFS_STATE_ERROR        0x0002      /* Consistency error detected */
#define OWFS_STATE_LOCKED       0x0004      /* Locked for consistency check */

typedef enum {
    OWFS_OK                     = 0,
    OWFS_ERR_INVALID_MAGIC      = 1,
    OWFS_ERR_CORRUPT_SUPERBLOCK = 2,
    OWFS_ERR_CHECKSUM_MISMATCH  = 3,
    OWFS_ERR_NO_FREE_BLOCKS     = 4,
    OWFS_ERR_NO_FREE_INODES     = 5,
    OWFS_ERR_NAME_TOO_LONG      = 6,
    OWFS_ERR_NOT_FOUND          = 7,
    OWFS_ERR_ALREADY_EXISTS     = 8,
    OWFS_ERR_NOT_CATALOG        = 9,
    OWFS_ERR_NOT_FILE           = 10,
    OWFS_ERR_IO                 = 11,
    OWFS_ERR_BUFFER_TOO_SMALL   = 12,
    OWFS_ERR_CATALOG_FULL       = 13,
    OWFS_ERR_VOLUME_DIRTY       = 14,
    OWFS_ERR_WRITE_PROTECTED    = 15,
    OWFS_ERR_INVALID_PARAM      = 16
} owfs_status_t;

#endif /* OWFS_TYPES_H */
