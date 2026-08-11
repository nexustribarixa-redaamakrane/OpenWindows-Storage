#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <assert.h>

#include "../include/owfs_types.h"
#include "../include/owfs_superblock.h"
#include "../include/owfs_inode.h"
#include "../include/owfs_catalog.h"
#include "../include/owfs_blockmap.h"
#include "../include/owfs_file.h"
#include "../include/owfs_sync.h"
#include "../include/owfs_format.h"
#include "../../common/include/ow_mem.h"
#include "../../common/include/ow_sec.h"

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

static uint32_t g_entropy_state = 0x12345678U;
static htl_status_t ramdisk_entropy(void *ctx, uint8_t *out, size_t len) {
    (void)ctx;
    for (size_t i = 0; i < len; ++i) {
        g_entropy_state = g_entropy_state * 1664525U + 1013904223U;
        out[i] = (uint8_t)(g_entropy_state >> 16);
    }
    return HTL_OK;
}

#define BIG_SIZE (11 * OWFS_BLOCK_SIZE) /* crosses direct -> indirect boundary */

static uint8_t g_big[BIG_SIZE];

int main(void) {
    printf("[OWFS] Starting unit test suite...\n");

    assert(sizeof(owfs_superblock_t) == 0x1000);
    assert(sizeof(owfs_inode_t) == 0x100);
    assert(sizeof(owfs_catalog_entry_t) == 0x100);
    printf("  [PASS] Struct packing sizes: Superblock 4096B, Inode 256B, Catalog 256B\n");

    htl_device_t dev;
    ow_memset(&dev, 0, sizeof(dev));
    dev.read_block = ramdisk_read;
    dev.write_block = ramdisk_write;
    dev.flush_cache = ramdisk_flush;
    dev.entropy = ramdisk_entropy;
    dev.block_size = OWFS_BLOCK_SIZE;
    dev.total_blocks = RAMDISK_BLOCKS;
    dev.device_type = HTL_DEV_AHCI_SATA;

    const uint8_t label[] = "OpenWindows_Sys";
    owfs_status_t res = owfs_format_volume(&dev, RAMDISK_BLOCKS, label, sizeof(label) - 1);
    assert(res == OWFS_OK);
    printf("  [PASS] Volume format completed cleanly\n");

    owfs_superblock_t sb;
    res = owfs_superblock_read(&dev, &sb);
    assert(res == OWFS_OK);
    assert(sb.magic == OWFS_MAGIC);
    assert(sb.total_blocks == RAMDISK_BLOCKS);
    assert(owfs_superblock_validate(&sb));
    assert(sb.crypto_nonce[0] != 0 || sb.crypto_nonce[1] != 0 || sb.crypto_nonce[2] != 0);
    printf("  [PASS] Superblock read & CRC32c validation succeeded\n");
    printf("  [PASS] Per-volume ChaCha20 nonce generated at format\n");

    res = owfs_mount(&dev, &sb);
    assert(res == OWFS_OK);
    assert(sb.mount_count == 1);
    assert(owfs_is_dirty(&sb));
    printf("  [PASS] Clean mount succeeded (mount_count=%u, volume now DIRTY)\n", sb.mount_count);

    uint32_t f_ino = 0, d_ino = 0;
    const uint8_t fname[] = "system_kernel.bin";
    const uint8_t dname[] = "Drivers";
    res = owfs_catalog_create(&dev, &sb, OWFS_ROOT_INODE, fname, sizeof(fname) - 1, &f_ino);
    assert(res == OWFS_OK);
    assert(f_ino > 0);
    res = owfs_catalog_mkdir(&dev, &sb, OWFS_ROOT_INODE, dname, sizeof(dname) - 1, &d_ino);
    assert(res == OWFS_OK);
    assert(d_ino > 0);
    printf("  [PASS] Created file inode #%u and catalog inode #%u under root\n", f_ino, d_ino);

    owfs_inode_t inode;
    res = owfs_inode_read(&dev, &sb, f_ino, &inode);
    assert(res == OWFS_OK);
    assert(inode.entry_type == OWFS_ENTRY_FILE);
    assert(inode.parent_inode == OWFS_ROOT_INODE);

    for (uint32_t i = 0; i < BIG_SIZE; ++i) {
        g_big[i] = (uint8_t)((i * 31U) ^ (i >> 8));
    }
    uint32_t written = 0;
    res = owfs_file_write(&dev, &sb, &inode, f_ino, g_big, 0, BIG_SIZE, &written);
    assert(res == OWFS_OK);
    assert(written == BIG_SIZE);
    assert(inode.size_bytes == BIG_SIZE);
    assert(inode.block_count >= 11);
    printf("  [PASS] Wrote %u bytes across %u blocks (crosses direct->indirect)\n",
           inode.size_bytes, inode.block_count);

    owfs_inode_t on_disk;
    res = owfs_inode_read(&dev, &sb, f_ino, &on_disk);
    assert(res == OWFS_OK);
    assert(on_disk.size_bytes == BIG_SIZE);

    static uint8_t readbuf[BIG_SIZE];
    uint32_t rd = 0;
    res = owfs_file_read(&dev, &sb, &on_disk, readbuf, 0, BIG_SIZE, &rd);
    assert(res == OWFS_OK);
    assert(rd == BIG_SIZE);
    assert(ow_memcmp(readbuf, g_big, BIG_SIZE) == 0);
    printf("  [PASS] Read back %u bytes, content matches byte-for-byte\n", rd);

    uint32_t clamped = 0;
    res = owfs_file_read(&dev, &sb, &on_disk, readbuf, 0, BIG_SIZE * 4, &clamped);
    assert(res == OWFS_OK);
    assert(clamped == BIG_SIZE);
    printf("  [PASS] Read clamped at EOF (%u bytes)\n", clamped);

    const uint8_t patch[] = {0xDE, 0xAD, 0xBE, 0xEF};
    res = owfs_file_write(&dev, &sb, &on_disk, f_ino, patch, sizeof(patch), sizeof(patch), &written);
    assert(res == OWFS_OK);
    assert(written == sizeof(patch));
    assert(on_disk.size_bytes == BIG_SIZE);
    printf("  [PASS] Partial overwrite did not truncate file (size=%u)\n", on_disk.size_bytes);

    static uint8_t smallbuf[8];
    uint32_t srd = 0;
    res = owfs_file_read(&dev, &sb, &on_disk, smallbuf, 0, sizeof(smallbuf), &srd);
    assert(res == OWFS_OK);
    assert(srd == sizeof(smallbuf));
    assert(smallbuf[0] == g_big[0]);
    assert(smallbuf[4] == 0xDE && smallbuf[7] == 0xEF);
    printf("  [PASS] Overwritten bytes verified at offset 4\n");

    res = owfs_inode_read(&dev, &sb, d_ino, &inode);
    assert(res == OWFS_OK);
    assert(inode.entry_type == OWFS_ENTRY_CATALOG);

    uint32_t found = 0;
    res = owfs_catalog_lookup(&dev, &sb, OWFS_ROOT_INODE, fname, sizeof(fname) - 1, &found);
    assert(res == OWFS_OK);
    assert(found == f_ino);
    res = owfs_catalog_lookup(&dev, &sb, OWFS_ROOT_INODE, dname, sizeof(dname) - 1, &found);
    assert(res == OWFS_OK);
    assert(found == d_ino);
    printf("  [PASS] Catalog lookup of file & directory succeeded\n");

    owfs_catalog_entry_t entries[16];
    uint32_t count = 0;
    res = owfs_catalog_list(&dev, &sb, OWFS_ROOT_INODE, entries, 16, &count);
    assert(res == OWFS_OK);
    assert(count == 2);
    printf("  [PASS] Root catalog lists %u entries\n", count);

    res = owfs_sync_changes(&dev, &sb);
    assert(res == OWFS_OK);
    assert(!owfs_is_dirty(&sb));
    printf("  [PASS] Sync committed bitmap Fletcher-64 & marked volume clean\n");

    res = owfs_mark_dirty(&dev, &sb);
    assert(res == OWFS_OK);
    assert(owfs_is_dirty(&sb));

    uint32_t corrupt_count = 0;
    res = owfs_consistency_check(&dev, &sb, &corrupt_count);
    assert(res == OWFS_OK);
    assert(corrupt_count == 0);
    assert(!owfs_is_dirty(&sb));
    printf("  [PASS] Dirty state consistency check & auto-recovery verified\n");

    res = owfs_unmount(&dev, &sb);
    assert(res == OWFS_OK);
    assert(!owfs_is_dirty(&sb));
    printf("  [PASS] Clean unmount completed\n");

    res = owfs_mount(&dev, &sb);
    assert(res == OWFS_OK);
    assert(sb.mount_count == 2);
    printf("  [PASS] Second clean mount (mount_count=%u)\n", sb.mount_count);

    res = owfs_mount(&dev, &sb);
    assert(res == OWFS_ERR_VOLUME_DIRTY);
    assert(sb.state_flags & OWFS_STATE_LOCKED);
    printf("  [PASS] Crash-style re-mount refused (LOCKED, VOLUME_DIRTY)\n");

    corrupt_count = 0;
    res = owfs_consistency_check(&dev, &sb, &corrupt_count);
    assert(res == OWFS_OK);
    assert(corrupt_count == 0);
    res = owfs_mount(&dev, &sb);
    assert(res == OWFS_OK);
    printf("  [PASS] Consistency check cleared lock, mount recovered\n");

    res = owfs_inode_read(&dev, &sb, f_ino, &on_disk);
    assert(res == OWFS_OK);
    res = owfs_file_truncate(&dev, &sb, &on_disk, f_ino, OWFS_BLOCK_SIZE);
    assert(res == OWFS_OK);
    assert(on_disk.size_bytes == OWFS_BLOCK_SIZE);
    assert(on_disk.block_count == 1);
    printf("  [PASS] Truncate shrank file to %u bytes / %u block(s)\n",
           on_disk.size_bytes, on_disk.block_count);

    /* ---- OWFS ChaCha20 encryption round-trip ---- */
    uint8_t key[OWFS_KEY_SIZE];
    for (uint32_t i = 0; i < sizeof(key); ++i) {
        key[i] = (uint8_t)(i * 5U + 3);
    }
    res = owfs_crypto_set_key(&dev, &sb, key, sizeof(key));
    assert(res == OWFS_OK);
    assert(sb.security_flags & OWFS_SEC_ENCRYPTED);

    const uint8_t secret_name[] = "secret.bin";
    uint32_t secret_ino = 0;
    res = owfs_catalog_create(&dev, &sb, OWFS_ROOT_INODE, secret_name, sizeof(secret_name) - 1, &secret_ino);
    assert(res == OWFS_OK);
    owfs_inode_t sec_inode;
    res = owfs_inode_read(&dev, &sb, secret_ino, &sec_inode);
    assert(res == OWFS_OK);
    assert(sec_inode.security_flags & OWFS_SEC_ENCRYPTED);

    static uint8_t sec_data[16 * OWFS_BLOCK_SIZE];
    for (uint32_t i = 0; i < sizeof(sec_data); ++i) {
        sec_data[i] = (uint8_t)(i ^ 0x3C);
    }
    uint32_t sec_written = 0;
    res = owfs_file_write(&dev, &sb, &sec_inode, secret_ino, sec_data, 0, sizeof(sec_data), &sec_written);
    assert(res == OWFS_OK);
    assert(sec_written == sizeof(sec_data));

    static uint8_t raw_block[OWFS_BLOCK_SIZE];
    uint32_t bnum = 0;
    res = owfs_blockmap_get(&dev, &sec_inode, 0, &bnum);
    assert(res == OWFS_OK);
    htl_status_t hres = htl_read_block(&dev, bnum, raw_block);
    assert(hres == HTL_OK);
    assert(ow_memcmp(raw_block, sec_data, sizeof(sec_data)) != 0);

    static uint8_t sec_read[sizeof(sec_data)];
    uint32_t sec_rd = 0;
    res = owfs_file_read(&dev, &sb, &sec_inode, sec_read, 0, sizeof(sec_data), &sec_rd);
    assert(res == OWFS_OK);
    assert(sec_rd == sizeof(sec_data));
    assert(ow_memcmp(sec_read, sec_data, sizeof(sec_data)) == 0);
    printf("  [PASS] OWFS ChaCha20 encryption round-trip verified (raw block differs)\n");

    /* ---- Permission enforcement (ownership) ---- */
    static uint8_t denybuf[8];
    ow_memset(denybuf, 0, sizeof(denybuf));
    res = owfs_inode_read(&dev, &sb, f_ino, &inode);
    assert(res == OWFS_OK);
    inode.permissions = 0x180; /* 0600: owner rw only */
    res = owfs_inode_write(&dev, &sb, f_ino, &inode);
    assert(res == OWFS_OK);

    ow_identity_t uid;
    uid.uid = 1000;
    uid.gid = 1000;
    ow_sec_set_identity(&uid);
    uint32_t denied = 0;
    res = owfs_file_read(&dev, &sb, &inode, denybuf, 0, sizeof(denybuf), &denied);
    assert(res == OWFS_ERR_ACCESS_DENIED);
    res = owfs_file_write(&dev, &sb, &inode, f_ino, denybuf, 0, sizeof(denybuf), &written);
    assert(res == OWFS_ERR_ACCESS_DENIED);
    printf("  [PASS] Non-owner denied read/write on 0600 inode (ACCESS_DENIED)\n");

    ow_sec_reset();
    inode.owner_uid = 1000;
    inode.owner_gid = 1000;
    res = owfs_inode_write(&dev, &sb, f_ino, &inode);
    assert(res == OWFS_OK);
    ow_sec_set_identity(&uid);
    res = owfs_file_write(&dev, &sb, &inode, f_ino, denybuf, 0, sizeof(denybuf), &written);
    assert(res == OWFS_OK);
    assert(written == sizeof(denybuf));
    printf("  [PASS] Owner (uid 1000) granted write after ownership transfer\n");
    ow_sec_reset();

    /* ---- HIDDEN inode ---- */
    sec_inode.security_flags |= OWFS_SEC_HIDDEN;
    res = owfs_inode_write(&dev, &sb, secret_ino, &sec_inode);
    assert(res == OWFS_OK);

    ow_identity_t stranger;
    stranger.uid = 2000;
    stranger.gid = 2000;
    ow_sec_set_identity(&stranger);
    uint32_t lfound = 0;
    res = owfs_catalog_lookup(&dev, &sb, OWFS_ROOT_INODE, secret_name, sizeof(secret_name) - 1, &lfound);
    assert(res == OWFS_ERR_NOT_FOUND);
    ow_sec_reset();
    res = owfs_catalog_lookup(&dev, &sb, OWFS_ROOT_INODE, secret_name, sizeof(secret_name) - 1, &lfound);
    assert(res == OWFS_OK);
    assert(lfound == secret_ino);
    printf("  [PASS] HIDDEN inode invisible to non-owner, visible to root\n");

    /* ---- Volume-level READONLY ---- */
    sb.security_flags |= OWFS_SEC_READONLY;
    res = owfs_superblock_write(&dev, &sb);
    assert(res == OWFS_OK);
    res = owfs_inode_read(&dev, &sb, f_ino, &inode);
    assert(res == OWFS_OK);
    res = owfs_file_write(&dev, &sb, &inode, f_ino, denybuf, 0, sizeof(denybuf), &written);
    assert(res == OWFS_ERR_WRITE_PROTECTED);
    printf("  [PASS] OWFS_SEC_READONLY volume rejects writes\n");
    sb.security_flags &= ~OWFS_SEC_READONLY;
    res = owfs_superblock_write(&dev, &sb);
    assert(res == OWFS_OK);

    res = owfs_full_scrub(&dev, RAMDISK_BLOCKS, label, sizeof(label) - 1);
    assert(res == OWFS_OK);
    printf("  [PASS] Full scrub & format completed successfully\n");

    printf("\n[OWFS] ALL TESTS PASSED SUCCESSFULLY!\n");
    return 0;
}
