#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <assert.h>

#include "../include/usfs_types.h"
#include "../include/usfs_superblock.h"
#include "../include/usfs_entry.h"
#include "../include/usfs_bitmap.h"
#include "../include/usfs_file.h"
#include "../include/usfs_sync.h"
#include "../include/usfs_format.h"
#include "../../common/include/ow_mem.h"
#include "../../common/include/ow_crypto.h"

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

#define P1_LEN 9000
#define P2_OFF 20000
#define P2_LEN 20000

static uint8_t g_p1[P1_LEN];
static uint8_t g_p2[P2_LEN];
static uint8_t g_sec[64 * 4];

int main(void) {
    printf("[USFS] Starting unit test suite...\n");

    assert(sizeof(usfs_superblock_t) == 0x1000);
    assert(sizeof(usfs_entry_t) == 0x100);
    printf("  [PASS] Struct packing sizes: Superblock 4096B, Entry 256B\n");

    htl_device_t dev;
    ow_memset(&dev, 0, sizeof(dev));
    dev.read_block = ramdisk_read;
    dev.write_block = ramdisk_write;
    dev.flush_cache = ramdisk_flush;
    dev.block_size = USFS_BLOCK_SIZE;
    dev.total_blocks = RAMDISK_BLOCKS;
    dev.device_type = HTL_DEV_USB_MASS;

    const uint8_t label[] = "USFS_USB_Drive";
    usfs_status_t res = usfs_format_volume(&dev, RAMDISK_BLOCKS, label, sizeof(label) - 1);
    assert(res == USFS_OK);
    printf("  [PASS] USFS volume format completed cleanly\n");

    usfs_superblock_t sb;
    res = usfs_superblock_read(&dev, &sb);
    assert(res == USFS_OK);
    assert(sb.magic == USFS_MAGIC);
    assert(sb.total_blocks == RAMDISK_BLOCKS);
    assert(usfs_superblock_validate(&sb));
    assert(sb.used_entries == 1);
    printf("  [PASS] Superblock read & CRC32c validation succeeded (root catalog allocated)\n");

    const uint8_t fname[] = "payload.dat";
    uint32_t eidx = 0;
    res = usfs_entry_alloc(&dev, &sb, USFS_ENTRY_FILE, fname, sizeof(fname) - 1, 0, &eidx);
    assert(res == USFS_OK);
    assert(eidx > 0);

    usfs_entry_t entry;
    res = usfs_entry_read(&dev, &sb, eidx, &entry);
    assert(res == USFS_OK);
    assert(entry.entry_type == USFS_ENTRY_FILE);
    assert(entry.parent_entry == 0);
    assert(entry.block_count == 0);
    printf("  [PASS] File entry allocated (slot %u) without data blocks\n", eidx);

    for (uint32_t i = 0; i < P1_LEN; ++i) {
        g_p1[i] = (uint8_t)((i * 7U) ^ (i >> 3));
    }
    uint32_t written = 0;
    res = usfs_file_write(&dev, &sb, &entry, eidx, g_p1, 0, P1_LEN, &written);
    assert(res == USFS_OK);
    assert(written == P1_LEN);
    assert(entry.size_bytes == P1_LEN);
    assert(entry.block_count == 3);
    printf("  [PASS] Wrote %u bytes (3 contiguous blocks)\n", entry.size_bytes);

    usfs_entry_t on_disk;
    res = usfs_entry_read(&dev, &sb, eidx, &on_disk);
    assert(res == USFS_OK);
    assert(on_disk.size_bytes == P1_LEN);

    static uint8_t readbuf[40000];
    uint32_t rd = 0;
    res = usfs_file_read(&dev, &sb, &on_disk, readbuf, 0, P1_LEN, &rd);
    assert(res == USFS_OK);
    assert(rd == P1_LEN);
    assert(ow_memcmp(readbuf, g_p1, P1_LEN) == 0);
    printf("  [PASS] Read back %u bytes, content matches byte-for-byte\n", rd);

    for (uint32_t i = 0; i < P2_LEN; ++i) {
        g_p2[i] = (uint8_t)((i * 13U) ^ (i >> 2));
    }
    res = usfs_file_write(&dev, &sb, &on_disk, eidx, g_p2, P2_OFF, P2_LEN, &written);
    assert(res == USFS_OK);
    assert(written == P2_LEN);
    assert(on_disk.size_bytes == P2_OFF + P2_LEN);
    assert(on_disk.block_count == 10);
    printf("  [PASS] Growth relocated file to %u bytes / %u blocks\n", on_disk.size_bytes, on_disk.block_count);

    res = usfs_entry_read(&dev, &sb, eidx, &on_disk);
    assert(res == USFS_OK);
    res = usfs_file_read(&dev, &sb, &on_disk, readbuf, 0, P1_LEN, &rd);
    assert(res == USFS_OK);
    assert(ow_memcmp(readbuf, g_p1, P1_LEN) == 0);
    res = usfs_file_read(&dev, &sb, &on_disk, readbuf + P2_OFF, P2_OFF, P2_LEN, &rd);
    assert(res == USFS_OK);
    assert(rd == P2_LEN);
    assert(ow_memcmp(readbuf + P2_OFF, g_p2, P2_LEN) == 0);
    printf("  [PASS] Post-relocation data verified at both offsets\n");

    uint32_t found = 0;
    res = usfs_entry_lookup(&dev, &sb, 0, fname, sizeof(fname) - 1, &found);
    assert(res == USFS_OK);
    assert(found == eidx);

    const uint8_t dname[] = "Docs";
    uint32_t didx = 0;
    res = usfs_entry_alloc(&dev, &sb, USFS_ENTRY_CATALOG, dname, sizeof(dname) - 1, 0, &didx);
    assert(res == USFS_OK);
    assert(didx > 0);
    res = usfs_entry_lookup(&dev, &sb, 0, dname, sizeof(dname) - 1, &found);
    assert(res == USFS_OK);
    assert(found == didx);
    printf("  [PASS] Catalog entry allocation & lookup verified\n");

    const uint8_t tmp_name[] = "temp.bin";
    uint32_t tmp_idx = 0;
    res = usfs_entry_alloc(&dev, &sb, USFS_ENTRY_FILE, tmp_name, sizeof(tmp_name) - 1, 0, &tmp_idx);
    assert(res == USFS_OK);
    usfs_entry_t tmp_entry;
    res = usfs_entry_read(&dev, &sb, tmp_idx, &tmp_entry);
    assert(res == USFS_OK);
    uint8_t tmp_data[4096];
    ow_memset(tmp_data, 0x5A, sizeof(tmp_data));
    res = usfs_file_write(&dev, &sb, &tmp_entry, tmp_idx, tmp_data, 0, sizeof(tmp_data), &written);
    assert(res == USFS_OK);
    assert(written == sizeof(tmp_data));
    uint32_t free_before = sb.free_blocks;
    res = usfs_entry_free(&dev, &sb, tmp_idx);
    assert(res == USFS_OK);
    assert(sb.free_blocks == free_before + 1);
    printf("  [PASS] Entry free released its data block (free_blocks=%u)\n", sb.free_blocks);

    res = usfs_flush_dirty(&dev, &sb);
    assert(res == USFS_OK);
    assert(!usfs_is_dirty(&sb));
    printf("  [PASS] Commit refreshed bitmap Fletcher-64\n");

    uint8_t key[USFS_KEY_SIZE];
    for (uint32_t i = 0; i < sizeof(key); ++i) {
        key[i] = (uint8_t)(i * 3U + 1);
    }
    res = usfs_crypto_set_key(&dev, &sb, key, sizeof(key));
    assert(res == USFS_OK);
    assert(sb.security_flags & USFS_SEC_ENCRYPTED);
    printf("  [PASS] ChaCha20 volume key installed (encryption enabled)\n");

    const uint8_t sec_name[] = "secret.txt";
    uint32_t sec_idx = 0;
    res = usfs_entry_alloc(&dev, &sb, USFS_ENTRY_FILE, sec_name, sizeof(sec_name) - 1, 0, &sec_idx);
    assert(res == USFS_OK);
    usfs_entry_t sec_entry;
    res = usfs_entry_read(&dev, &sb, sec_idx, &sec_entry);
    assert(res == USFS_OK);
    assert(sec_entry.security_flags & USFS_SEC_ENCRYPTED);
    for (uint32_t i = 0; i < sizeof(g_sec); ++i) {
        g_sec[i] = (uint8_t)(i ^ 0xA5);
    }
    res = usfs_file_write(&dev, &sb, &sec_entry, sec_idx, g_sec, 0, sizeof(g_sec), &written);
    assert(res == USFS_OK);
    assert(written == sizeof(g_sec));

    static uint8_t raw_block[USFS_BLOCK_SIZE];
    htl_status_t hres = htl_read_block(&dev, sec_entry.first_block, raw_block);
    assert(hres == HTL_OK);
    assert(ow_memcmp(raw_block, g_sec, sizeof(g_sec)) != 0);
    printf("  [PASS] Raw encrypted block differs from plaintext\n");

    static uint8_t sec_read[sizeof(g_sec)];
    uint32_t srd = 0;
    res = usfs_file_read(&dev, &sb, &sec_entry, sec_read, 0, sizeof(g_sec), &srd);
    assert(res == USFS_OK);
    assert(srd == sizeof(g_sec));
    assert(ow_memcmp(sec_read, g_sec, sizeof(g_sec)) == 0);
    printf("  [PASS] Encrypted file round-trip verified\n");

    res = usfs_flush_dirty(&dev, &sb);
    assert(res == USFS_OK);

    res = usfs_mark_dirty(&dev, &sb);
    assert(res == USFS_OK);
    assert(usfs_is_dirty(&sb));

    uint32_t corrupt_count = 0;
    res = usfs_consistency_check(&dev, &sb, &corrupt_count);
    assert(res == USFS_OK);
    assert(corrupt_count == 0);
    assert(!usfs_is_dirty(&sb));
    printf("  [PASS] Dirty state consistency check & auto-recovery verified\n");

    res = usfs_mount(&dev, &sb);
    assert(res == USFS_OK);
    assert(sb.mount_count == 1);
    assert(ow_memcmp(sb.key_slot_1, key, USFS_KEY_SIZE) == 0);
    printf("  [PASS] Clean mount carried encryption key (mount_count=1)\n");

    res = usfs_mount(&dev, &sb);
    assert(res == USFS_ERR_VOLUME_DIRTY);
    assert(sb.state_flags & USFS_STATE_LOCKED);
    printf("  [PASS] Crash-style re-mount refused (LOCKED, VOLUME_DIRTY)\n");

    corrupt_count = 0;
    res = usfs_consistency_check(&dev, &sb, &corrupt_count);
    assert(res == USFS_OK);
    assert(corrupt_count == 0);
    res = usfs_mount(&dev, &sb);
    assert(res == USFS_OK);
    printf("  [PASS] Consistency check cleared lock, mount recovered\n");

    res = usfs_entry_read(&dev, &sb, eidx, &on_disk);
    assert(res == USFS_OK);
    res = usfs_file_truncate(&dev, &sb, &on_disk, eidx, USFS_BLOCK_SIZE);
    assert(res == USFS_OK);
    assert(on_disk.size_bytes == USFS_BLOCK_SIZE);
    assert(on_disk.block_count == 1);
    printf("  [PASS] Truncate shrank file to 1 block\n");

    res = usfs_crypto_purge(&dev, &sb);
    assert(res == USFS_OK);
    assert((sb.security_flags & USFS_SEC_ENCRYPTED) == 0);
    assert(sb.key_slot_1[0] == 0xAA);
    assert(sb.key_slot_2[0] == 0xAA);
    printf("  [PASS] Cryptographic purge (key slot sanitization) verified\n");

    res = usfs_full_scrub(&dev, RAMDISK_BLOCKS, label, sizeof(label) - 1);
    assert(res == USFS_OK);
    printf("  [PASS] Full scrub & format completed successfully\n");

    printf("\n[USFS] ALL TESTS PASSED SUCCESSFULLY!\n");
    return 0;
}
