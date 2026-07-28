#ifndef USFS_TYPES_H
#define USFS_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "../../common/include/ow_htl.h"

#define USFS_BLOCK_SIZE         0x1000      /* 4096 bytes */
#define USFS_BLOCK_SHIFT        12          /* log2(0x1000) */
#define USFS_ENTRY_SIZE         0x100       /* 256 bytes per entry */
#define USFS_NAME_MAX_BYTES     128         /* SUTF-8 name field */
#define USFS_MAGIC              0x55534653UL /* 'USFS' */
#define USFS_VERSION_MAJOR      1
#define USFS_VERSION_MINOR      0
#define USFS_KEY_SLOT_COUNT     2           /* 2 active key slots */
#define USFS_KEY_SLOT_SIZE      0x100       /* 256 bytes per key slot */

/* Security flags */
#define USFS_SEC_ENCRYPTED      (1U << 0)   /* Volume-level encryption active */
#define USFS_SEC_READONLY       (1U << 1)
#define USFS_SEC_HIDDEN         (1U << 2)
#define USFS_SEC_SIGNED         (1U << 3)   /* Integrity signature present */

/* Entry types */
#define USFS_ENTRY_FILE         0x01
#define USFS_ENTRY_CATALOG      0x02
#define USFS_ENTRY_DELETED      0x80

/* Volume state flags (power-cut protection) */
#define USFS_STATE_CLEAN        0x0000
#define USFS_STATE_DIRTY        0x0001
#define USFS_STATE_ERROR        0x0002
#define USFS_STATE_LOCKED       0x0004

typedef enum {
    USFS_OK                     = 0,
    USFS_ERR_INVALID_MAGIC      = 1,
    USFS_ERR_CORRUPT_SUPERBLOCK = 2,
    USFS_ERR_CHECKSUM_MISMATCH  = 3,
    USFS_ERR_NO_FREE_BLOCKS     = 4,
    USFS_ERR_NO_FREE_ENTRIES    = 5,
    USFS_ERR_NAME_TOO_LONG      = 6,
    USFS_ERR_NOT_FOUND          = 7,
    USFS_ERR_ALREADY_EXISTS     = 8,
    USFS_ERR_IO                 = 9,
    USFS_ERR_BUFFER_TOO_SMALL   = 10,
    USFS_ERR_VOLUME_DIRTY       = 11,
    USFS_ERR_WRITE_PROTECTED    = 12,
    USFS_ERR_ENCRYPTED          = 13,
    USFS_ERR_KEY_INVALID        = 14,
    USFS_ERR_INVALID_PARAM      = 15
} usfs_status_t;

#endif /* USFS_TYPES_H */
