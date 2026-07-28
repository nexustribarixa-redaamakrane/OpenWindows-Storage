#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#if defined(__has_include)
  #if __has_include(<stdio.h>)
    #include <stdio.h>
  #endif
  #if __has_include(<assert.h>)
    #include <assert.h>
  #endif
#endif

#ifndef assert
  #define assert(cond) do { if (!(cond)) { for(;;); } } while(0)
#endif
#ifndef printf
  static inline int dummy_printf(const char *fmt, ...) { (void)fmt; return 0; }
  #define printf(...) dummy_printf(__VA_ARGS__)
#endif

#include "../include/usfs_types.h"
#include "../include/usfs_superblock.h"
#include "../include/usfs_entry.h"
#include "../include/usfs_sync.h"
#include "../include/usfs_format.h"
#include "../../common/include/ow_mem.h"

#define RAMDISK_BLOCKS 256
#define RAMDISK_SIZE   (RAMDISK_BLOCKS * USFS_BLOCK_SIZE)

static uint8_t g_ramdisk[RAMDISK_SIZE];

static htl_status_t ramdisk_read(void *ctx, uint32_t block_num, void *buf, uint32_t block_size) {
    (void)ctx;
    if (block_num >= RAMDISK_BLOCKS || block_size != USFS_BLOCK_SIZE) {
        return HTL_ERR_INVALID_BLOCK;
    }
    ow_memcpy(buf, g_ramdisk + (block_num * USFS_BLOCK_SIZE), USFS_BLOCK_SIZE);
    return HTL_OK;
}

static htl_status_t ramdisk_write(void *ctx, uint32_t block_num, const void *buf, uint32_t block_size) {
    (void)ctx;
    if (block_num >= RAMDISK_BLOCKS || block_size != USFS_BLOCK_SIZE) {
        return HTL_ERR_INVALID_BLOCK;
    }
    ow_memcpy(g_ramdisk + (block_num * USFS_BLOCK_SIZE), buf, USFS_BLOCK_SIZE);
    return HTL_OK;
}

static htl_status_t ramdisk_flush(void *ctx) {
    (void)ctx;
    return HTL_OK;
}

int main(void) {
    printf("[USFS] Starting unit test suite...\n");

    /* 1. Verify packed struct sizes */
    assert(sizeof(usfs_superblock_t) == 0x1000);
    assert(sizeof(usfs_entry_t) == 0x100);
    printf("  [PASS] Struct packing sizes: Superblock 4096B, Entry 256B\n");

    /* Setup RAM-backed HTL device */
    htl_device_t dev;
    ow_memset(&dev, 0, sizeof(dev));
    dev.read_block = ramdisk_read;
    dev.write_block = ramdisk_write;
    dev.flush_cache = ramdisk_flush;
    dev.block_size = USFS_BLOCK_SIZE;
    dev.total_blocks = RAMDISK_BLOCKS;
    dev.device_type = HTL_DEV_USB_MASS;

    /* 2. Test Volume Format */
    const uint8_t label[] = "USFS_USB_Drive";
    usfs_status_t res = usfs_format_volume(&dev, RAMDISK_BLOCKS, label, sizeof(label) - 1);
    assert(res == USFS_OK);
    printf("  [PASS] USFS volume format completed cleanly\n");

    /* 3. Read & Validate Superblock */
    usfs_superblock_t sb;
    res = usfs_superblock_read(&dev, &sb);
    assert(res == USFS_OK);
    assert(sb.magic == USFS_MAGIC);
    assert(sb.total_blocks == RAMDISK_BLOCKS);
    assert(usfs_superblock_validate(&sb));
    printf("  [PASS] Superblock read & CRC32c validation succeeded\n");

    /* 4. Test Entry Allocation & Lookup */
    uint32_t eidx1 = 0;
    res = usfs_entry_alloc(&dev, &sb, &eidx1);
    assert(res == USFS_OK);

    usfs_entry_t entry1;
    res = usfs_entry_read(&dev, &sb, eidx1, &entry1);
    assert(res == USFS_OK);

    const uint8_t fname1[] = "payload.dat";
    entry1.name_length = (uint8_t)(sizeof(fname1) - 1);
    ow_memcpy(entry1.name, fname1, sizeof(fname1) - 1);
    entry1.parent_entry = 0; /* Root catalog */
    res = usfs_entry_write(&dev, &sb, eidx1, &entry1);
    assert(res == USFS_OK);

    uint32_t found_idx = 0;
    res = usfs_entry_lookup(&dev, &sb, 0, fname1, sizeof(fname1) - 1, &found_idx);
    assert(res == USFS_OK);
    assert(found_idx == eidx1);
    printf("  [PASS] Entry allocation & SUTF-8 lookup succeeded\n");

    /* 5. Test Power-Cut State & Sync */
    res = usfs_mark_dirty(&dev, &sb);
    assert(res == USFS_OK);
    assert(usfs_is_dirty(&sb));

    uint32_t corrupt_count = 0;
    res = usfs_consistency_check(&dev, &sb, &corrupt_count);
    assert(res == USFS_OK);
    assert(corrupt_count == 0);
    assert(!usfs_is_dirty(&sb));
    printf("  [PASS] Dirty state consistency check & auto-recovery verified\n");

    /* 6. Test Cryptographic Purge (Key Slot Invalidation) */
    sb.security_flags |= USFS_SEC_ENCRYPTED;
    ow_memset(sb.key_slot_1, 0x42, sizeof(sb.key_slot_1));
    ow_memset(sb.key_slot_2, 0x84, sizeof(sb.key_slot_2));

    res = usfs_crypto_purge(&dev, &sb);
    assert(res == USFS_OK);
    assert((sb.security_flags & USFS_SEC_ENCRYPTED) == 0);
    assert(sb.key_slot_1[0] == 0xAA);
    assert(sb.key_slot_2[0] == 0xAA);
    printf("  [PASS] Cryptographic purge (key slot sanitization) verified\n");

    printf("\n[USFS] ALL TESTS PASSED SUCCESSFULLY!\n");
    return 0;
}
