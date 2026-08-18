# ow-storage: OpenWindows Storage & Filesystem Library Suite

Core C99 freestanding storage, block abstraction, and filesystem library suite for the **OpenWindows Operating System** ecosystem.

Dual-target static libraries:
- `libowfs.a` — OpenWindows File System (Native system drive filesystem)
- `libusfs.a` — Universal Secured File System (Portable, cryptographically flagged external media filesystem)

---

## Security Model

Both filesystems enforce a common identity-based access model (`ow_sec`):

- **Principals**: `ow_identity_t { uid, gid }`. Set via `ow_sec_set_identity()`. Defaults to UID 0 (superuser), which bypasses permission checks. The library is single-principal-at-a-time and host layers must serialize.
- **Ownership**: Every entry/inode stores `owner_uid` / `owner_gid` (assigned from the creating principal) plus a standard 9-bit `rwx` mode. Owner/group/other triplets are enforced on file read/write/truncate, catalog create/insert/remove, and inode free.
- **Read-Only Volumes**: Setting `USFS_SEC_READONLY` / `OWFS_SEC_READONLY` in the superblock rejects every write path with `ERR_WRITE_PROTECTED`.
- **Hidden Objects**: `USFS_SEC_HIDDEN` / `OWFS_SEC_HIDDEN` on an entry/inode makes it invisible to all callers except its owner and the superuser (lookup and list).
- **Entry Table Integrity Signing (USFS only)**: Setting `USFS_SEC_SIGNED` in the superblock enables CRC32c-based tamper-evidence over the entire USFS entry table. Computed via `usfs_signature_compute()` and stored in the superblock; verified during consistency checks via `usfs_signature_verify()`. This does not encrypt data but provides integrity attestation for the entry table.
- **Data-At-Rest Encryption (both filesystems)**: Real ChaCha20 (20 rounds, 256-bit key, 64-byte blocks). File data blocks are XORed with a keystream keyed by the volume key, a 12-byte per-volume nonce, and the physical block number as counter. Raw device reads yield ciphertext; encrypted inodes round-trip through the file API.
- **Per-Volume Nonce**: A fresh nonce is drawn from the device entropy callback (`htl_device_t.entropy`) at format time, so re-formatting with a reused key produces a distinct keystream.
- **Cryptographic Purge**: Overwrites and invalidates active key slots (`key_slot_1`, `key_slot_2`) 3 times (0xFF, 0x00, 0xAA) with physical cache flushes, rendering encrypted data unrecoverable. Available as `usfs_crypto_purge` / `owfs_crypto_purge`.
- **Limitations**: Key slots are stored in the superblock, so at-rest protection assumes the device itself is not in attacker hands. Permission enforcement is the library's responsibility; a VFS layer must supply the current identity.

---

## Architectural & Subsystem Features

1. **Freestanding C99 Compliance**: Built strictly with `-std=c99 -nostdlib -ffreestanding`. Zero dependencies on hosted C standard headers (`<stdio.h>`, `<stdlib.h>`, `<string.h>`). Uses custom memory routines (`ow_memcpy`, `ow_memset`, `ow_memcmp`).
2. **SuperUnicode & SUTF-8 Native**: All file paths, volume labels, and catalog names use SUTF-8 byte streams mapped to SuperUnicode 31-bit codepoints.
3. **MBL Partition Handoff**: `libowfs.a` starts at drive offset `0x10000` (reserving `0x0000`–`0xFFFF` for Modular Bootloader stage sectors).
4. **Catalogs vs. Folders**: Terminology strictly enforced — **Catalogs** at low-level disk structures (`owfs_catalog_entry_t`, `OWFS_ENTRY_CATALOG`) and **Folders** presented in userland GUI interfaces.
5. **Hexadecimal & Nibble-Aligned Sizing**: Block size `0x1000` (4096B), fixed inode/entry size `0x100` (256B), packed structs (`__attribute__((packed))`).

---

## Mandatory Core Driver Modules

1. **Hardware Translation Layer (HTL Driver Module)**: Physical block device abstraction (`htl_read_block`, `htl_write_block`, `htl_flush_cache`) via function-pointer-based driver callbacks. Type tags (`HTL_DEV_NVME`, `HTL_DEV_AHCI_SATA`, `HTL_DEV_USB_MASS`) identify the endpoint class; users supply the actual driver implementation.
2. **Corruption Detection & Checksumming**: Integrated CRC32c (Castagnoli) and Fletcher-64 block-level integrity verification embedded directly into Superblock, Inode, and Catalog headers.
3. **Power-Cut Protection & State Locking**: Real-time volume state tracking (`OWFS_STATE_DIRTY` / `USFS_STATE_DIRTY`). Abrupt power loss triggers volume locking and mandatory consistency scans (`owfs_consistency_check`) before writes are allowed.
4. **Direct Synchronous Flush**: Bare-metal disk synchronization routines (`owfs_sync_changes`, `usfs_flush_dirty`) forcing uncommitted changes directly to physical non-volatile storage.
5. **Formatting Protocols**: Automated volume synthesis (`owfs_format_volume`, `usfs_format_volume`) initializing clean Superblocks, Block Allocation Bitmaps, and Root Catalogs (`/`).
6. **Erasure & Secure Sanitization Protocols**:
   - **Quick Format**: Resets Superblock, bitmap allocator, and Root Catalog headers. Physical data region bytes are left on disk but metadata tracking them is destroyed (data is unrecoverable).
   - **Full Scrub**: Overwrites all usable sectors with `0x00` byte streams before formatting.
   - **Cryptographic Purge**: Overwrites and invalidates active key slots (`key_slot_1`, `key_slot_2`) 3 times (0xFF, 0x00, 0xAA) with physical cache flushes, rendering encrypted data unrecoverable. Available for both filesystems (`usfs_crypto_purge`, `owfs_crypto_purge`).

---

## Building and Testing

### Build with MinGW / GCC
```bash
# Configure build tree
cmake -B build -G "MinGW Makefiles" -DCMAKE_C_COMPILER=gcc

# Build static libraries (libowfs.a & libusfs.a)
cmake --build build

# Execute full test suite
ctest --test-dir build --output-on-failure
```

---

## License

Dual-licensed under the MIT License and Apache License 2.0.
