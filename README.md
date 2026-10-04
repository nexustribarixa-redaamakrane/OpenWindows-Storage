# OpenWindows Storage

This repository builds two freestanding C99 filesystem libraries over a
caller-supplied block-device interface:

- `owfs_static` (`libowfs.a`) — OWFS superblocks, inodes, catalogs, bitmap,
  block maps, file operations, formatting, and sync.
- `usfs_static` (`libusfs.a`) — USFS superblocks, fixed entry table, bitmap,
  file operations, formatting, and sync.

Both use the common implementation in `common/` for memory/string helpers,
checksums, ChaCha20, security identity checks, and HTL block I/O. The library
does not discover hardware or install a kernel VFS.

## Disk and device contract

The public headers are under `owfs/include/`, `usfs/include/`, and
`common/include/`. A host or kernel supplies an `htl_device_t` containing
read/write/flush callbacks, context, block size, and device capacity. Format
and file operations return the filesystem-specific `owfs_status_t` or
`usfs_status_t`; callers are responsible for handling I/O and corruption
errors.

OWFS and USFS use 4096-byte blocks and 256-byte inode/entry records. OWFS
reserves the first 64 KiB for the bootloader, placing its superblock at
relative block 16. Both formats store names as SUTF-8 byte strings. OWFS uses
inode/catalog structures and direct/indirect block mapping; USFS uses a
fixed-size entry table.

The APIs expose formatting, quick-format and scrub operations, dirty-state
sync/consistency handling, access modes, and optional data encryption. The
encryption implementation is ChaCha20 stream XOR; it does not authenticate
ciphertext. USFS `SIGNED` integrity metadata is CRC32c-based, not a
cryptographic signature. Key slots are stored in volume metadata, so these
mechanisms do not protect a device from an attacker who can read or modify the
whole medium.

## Build and tests

Requirements: CMake 3.10+ and a GCC-compatible C compiler. The library
compilation uses freestanding GCC-style flags.

```powershell
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_C_COMPILER=gcc
cmake --build build
ctest --test-dir build --output-on-failure
```

The CMake project builds the common object library and the OWFS/USFS static
libraries. CTest runs the host tests in `owfs/tests/test_owfs.c` and
`usfs/tests/test_usfs.c`; these use test devices and do not exercise a real
disk controller.

## Source map

- `common/include/ow_htl.h` — block-device callback contract.
- `common/include/ow_checksum.h`, `ow_crypto.h`, `ow_sec.h` — shared primitives.
- `owfs/include/owfs.h` and `usfs/include/usfs.h` — public umbrella headers.
- `owfs/src/`, `usfs/src/` — on-disk structures and operations.
- `owfs/tests/`, `usfs/tests/` — hosted test harnesses.

The library is a storage component, not a complete filesystem driver stack.
The host must provide the device callbacks, volume lifecycle, identity
serialization, and any higher-level path/VFS policy.
