#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "../include/ow_sec.h"
#include "../include/ow_mem.h"

static ow_identity_t g_current = { 0, 0 }; /* UID 0 = superuser */

void ow_sec_reset(void) {
    g_current.uid = 0;
    g_current.gid = 0;
}

void ow_sec_set_identity(const ow_identity_t *id) {
    if (!id) {
        ow_sec_reset();
        return;
    }
    g_current.uid = id->uid;
    g_current.gid = id->gid;
}

ow_identity_t ow_sec_current(void) {
    return g_current;
}

bool ow_sec_is_superuser(void) {
    return g_current.uid == 0;
}

bool ow_sec_access(uint16_t mode, uint16_t owner_uid, uint16_t owner_gid,
                   uint8_t want) {
    if (g_current.uid == 0) {
        return true; /* superuser bypasses permission checks */
    }

    uint16_t bits = 0;
    if (g_current.uid == owner_uid) {
        bits = (uint16_t)((mode >> 6) & 0x7);      /* owner triple */
    } else if (g_current.gid == owner_gid) {
        bits = (uint16_t)((mode >> 3) & 0x7);      /* group triple */
    } else {
        bits = (uint16_t)(mode & 0x7);             /* other triple */
    }

    if (want & OW_ACCESS_READ) {
        if (!(bits & (1u << 2))) {
            return false;
        }
    }
    if (want & OW_ACCESS_WRITE) {
        if (!(bits & (1u << 1))) {
            return false;
        }
    }
    if (want & OW_ACCESS_EXEC) {
        if (!(bits & (1u << 0))) {
            return false;
        }
    }
    return true;
}
