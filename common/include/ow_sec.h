#ifndef OW_SEC_H
#define OW_SEC_H

#include <stdbool.h>
#include <stdint.h>

/* Identity of the principal performing filesystem operations. */
typedef struct {
    uint32_t uid;
    uint32_t gid;
} ow_identity_t;

/* Access capabilities requested of a mode's rwx triplets. */
#define OW_ACCESS_READ   (1u << 0)
#define OW_ACCESS_WRITE  (1u << 1)
#define OW_ACCESS_EXEC   (1u << 2)

/* The library enforces access against a single current principal at a time.
 * Host layers must serialize access, mirroring the single-user design of the
 * rest of this freestanding library. The identity defaults to UID 0, the
 * superuser, which bypasses all permission checks. */

void ow_sec_reset(void);
void ow_sec_set_identity(const ow_identity_t *id);
ow_identity_t ow_sec_current(void);
bool ow_sec_is_superuser(void);

/* Resolve the 9-bit `mode` (owner/group/other rwx triplets) against the
 * current principal. UID 0 always succeeds. Returns true when every bit in
 * `want` is granted. */
bool ow_sec_access(uint16_t mode, uint16_t owner_uid, uint16_t owner_gid,
                   uint8_t want);

#endif /* OW_SEC_H */
