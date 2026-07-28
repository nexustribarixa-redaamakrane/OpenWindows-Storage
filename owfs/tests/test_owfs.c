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

#include "../include/owfs_types.h"
#include "../include/owfs_superblock.h"
#include "../include/owfs_inode.h"
#include "../include/owfs_catalog.h"
#include "../include/owfs_sync.h"
#include "../include/owfs_format.h"
#include "../../common/include/ow_mem.h"

#define RAMDISK_BLOCKS 512
#define RAMDISK_SIZE   (RAMDISK_BLOCKS * OWFS_BLOCK_SIZE)

static uint8_t g_ramdisk[RAMDISK_SIZE];

static htl_status_t ramdisk_read(void *ctx, uint32_t block_num, void *buf, uint32_t block_size) {
    (void)ctx;
    if (block_num >= RAMDISK_BLOCKS || block_size != OWFS_BLOCK_SIZE) {
        return HTL_ERR_INVALID_BLOCK;
    }
    ow_memcpy(buf, g_ramdisk + (block_num * OWFS_BLOCK_SIZE), OWFS_BLOCK_SIZE);
    return HTL_OK;
}

static htl_status_t ramdisk_write(void *ctx, uint32_t block_num, const void *buf, uint32_t block_size) {
    (void)ctx;
    if (block_num >= RAMDISK_BLOCKS || block_size != OWFS_BLOCK_SIZE) {
        return HTL_ERR_INVALID_BLOCK;
    }
    ow_memcpy(g_ramdisk + (block_num * OWFS_BLOCK_SIZE), buf, OWFS_BLOCK_SIZE);
    return HTL_OK;
}

static htl_status_t ramdisk_flush(void *ctx) {
    (void)ctx;
    return HTL_OK;
}

int main(void) {
    printf("[OWFS] Starting unit test suite...\n");

    /* 1. Verify packed struct sizes */
    assert(sizeof(owfs_superblock_t) == 0x1000);
    assert(sizeof(owfs_inode_t) == 0x100);
    assert(sizeof(owfs_catalog_entry_t) == 0x100);
    printf("  [PASS] Struct packing sizes: Superblock 4096B, Inode 256B, Catalog 256B\n");

    /* Setup RAM-backed HTL device */
    htl_device_t dev;
    ow_memset(&dev, 0, sizeof(dev));
    dev.read_block = ramdisk_read;
    dev.write_block = ramdisk_write;
    dev.flush_cache = ramdisk_flush;
    dev.block_size = OWFS_BLOCK_SIZE;
    dev.total_blocks = RAMDISK_BLOCKS;
    dev.device_type = HTL_DEV_AHCI_SATA;

    /* 2. Test Volume Format */
    const uint8_t label[] = "OpenWindows_Sys";
    owfs_status_t res = owfs_format_volume(&dev, RAMDISK_BLOCKS, label, sizeof(label) - 1);
    assert(res == OWFS_OK);
    printf("  [PASS] Volume format completed cleanly\n");

    /* 3. Read & Validate Superblock */
    owfs_superblock_t sb;
    res = owfs_superblock_read(&dev, &sb);
    assert(res == OWFS_OK);
    assert(sb.magic == OWFS_MAGIC);
    assert(sb.total_blocks == RAMDISK_BLOCKS);
    assert(owfs_superblock_validate(&sb));
    printf("  [PASS] Superblock read & CRC32c validation succeeded\n");

    /* 4. Test Inode Allocation */
    uint32_t ino1 = 0, ino2 = 0;
    res = owfs_inode_alloc(&dev, &sb, &ino1);
    assert(res == OWFS_OK);
    assert(ino1 > 0);

    res = owfs_inode_alloc(&dev, &sb, &ino2);
    assert(res == OWFS_OK);
    assert(ino2 > ino1);
    printf("  [PASS] Inode allocation succeeded (Inodes #%u, #%u)\n", ino1, ino2);

    /* 5. Test Catalog Insertion & Lookup */
    const uint8_t fname1[] = "system_kernel.bin";
    res = owfs_catalog_insert(&dev, &sb, OWFS_ROOT_INODE, fname1, sizeof(fname1) - 1, ino1, OWFS_ENTRY_FILE);
    assert(res == OWFS_OK);

    const uint8_t cname1[] = "Drivers";
    res = owfs_catalog_insert(&dev, &sb, OWFS_ROOT_INODE, cname1, sizeof(cname1) - 1, ino2, OWFS_ENTRY_CATALOG);
    assert(res == OWFS_OK);

    uint32_t found_ino = 0;
    res = owfs_catalog_lookup(&dev, &sb, OWFS_ROOT_INODE, fname1, sizeof(fname1) - 1, &found_ino);
    assert(res == OWFS_OK);
    assert(found_ino == ino1);

    res = owfs_catalog_lookup(&dev, &sb, OWFS_ROOT_INODE, cname1, sizeof(cname1) - 1, &found_ino);
    assert(res == OWFS_OK);
    assert(found_ino == ino2);
    printf("  [PASS] Catalog insert & SUTF-8 lookup verified\n");

    /* 6. Test Catalog Enumeration */
    owfs_catalog_entry_t entries[16];
    uint32_t entry_count = 0;
    res = owfs_catalog_list(&dev, &sb, OWFS_ROOT_INODE, entries, 16, &entry_count);
    assert(res == OWFS_OK);
    assert(entry_count == 2);
    printf("  [PASS] Catalog listing returned %u entries\n", entry_count);

    /* 7. Test Catalog Removal */
    res = owfs_catalog_remove(&dev, &sb, OWFS_ROOT_INODE, fname1, sizeof(fname1) - 1);
    assert(res == OWFS_OK);

    res = owfs_catalog_lookup(&dev, &sb, OWFS_ROOT_INODE, fname1, sizeof(fname1) - 1, &found_ino);
    assert(res == OWFS_ERR_NOT_FOUND);
    printf("  [PASS] Catalog removal verified\n");

    /* 8. Test Power-Cut State & Sync */
    res = owfs_mark_dirty(&dev, &sb);
    assert(res == OWFS_OK);
    assert(owfs_is_dirty(&sb));

    uint32_t corrupt_count = 0;
    res = owfs_consistency_check(&dev, &sb, &corrupt_count);
    assert(res == OWFS_OK);
    assert(corrupt_count == 0);
    assert(!owfs_is_dirty(&sb));
    printf("  [PASS] Dirty state consistency check & auto-recovery verified\n");

    res = owfs_sync_changes(&dev, &sb);
    assert(res == OWFS_OK);
    printf("  [PASS] Synchronous flush (owfs_sync_changes) completed\n");

    /* 9. Test Scrub */
    res = owfs_full_scrub(&dev, RAMDISK_BLOCKS, label, sizeof(label) - 1);
    assert(res == OWFS_OK);
    printf("  [PASS] Full scrub & format completed successfully\n");

    printf("\n[OWFS] ALL TESTS PASSED SUCCESSFULLY!\n");
    return 0;
}
