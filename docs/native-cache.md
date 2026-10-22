# Automatic observed-instruction cache

The opt-in `b2l_native_cache=1` mode belongs to the deliberately stripped-pointer
ARM64e development ABI. It does not implement pointer authentication security,
claim PAC hardware, or translate arbitrary binaries statically.

## Boot lifecycle

1. Firmware locates unique SYSTEM and USERDATA partitions on the BOOT UFS LUN.
   It reads the clean journaled SYSTEM HFS cache file and validates its format,
   root identity, contiguous allocation and separation from HFS metadata and
   journal. EFI never writes either volume.
2. The 128 MiB snapshot is placed inside the allocated EFI conventional-memory
   arena, after the Mach-O image and before the Apple device tree/boot arguments.
   `topOfKernelData` reserves it; `/chosen/b2l,native-cache` describes its range.
   Existing framebuffer, firmware, DMA and EL2 reservations remain excluded.
3. XNU independently validates the SYSTEM file geometry and its private bounded
   writer. No writable whole-disk or SYSTEM BSD node is introduced. The existing
   16 MiB SYSTEM log tail remains separate.
4. Normal HFS root mounting performs USERDATA journal replay. Immediately before
   `load_init_program`, XNU validates cache records and resolves root CNIDs with
   VFS_VGET. It compares file size, Mach-O/cache UUID and the affected page bytes.
   Matching pages are updated through vn_rdwr and VNOP_FSYNC, then read back.
   Already translated pages are skipped; unrelated edits are never overwritten.
5. Only after successful emulation does the EL0 trap path enqueue a bounded
   observation. It performs no filesystem I/O, allocation or waiting for storage.
   A background kernel thread resolves the referenced VM map through constant
   submaps, shadows and backing pagers to the exact vnode/file offset. The file
   word must still match the observed instruction. It then persists a new record.

A cold cache learns during the first run. Changes are applied on the next normal
boot; no host regeneration or recovery flashing is required for each learned
instruction. It does not patch currently executing mappings. PACGA, LDRA and
unknown instructions continue through the existing emulator. The cache converts
PAC/AUT/XPAC identity operations, authenticated branches/returns and LDAPR (to
stronger LDAR). No scan of literal pools or unexecuted text is performed.

## Persistence and interrupted power

The fixed file contains two identical 4 KiB format/root headers, followed by
5461 dual-copy cells. Each 12 KiB copy contains a sequence number, root CNID,
file length, aligned page offset, Mach-O/cache UUID, original and translated
4 KiB pages, SHA-256 hashes and CRC32. Every changed word is independently
checked against the allowlisted native converter. PACGA cannot become a NOP.

The worker writes the alternate copy, flushes storage, reads it back and only
then publishes it to the in-memory index. The previous valid copy is retained.
Any persistence error disables learning for the rest of that boot. Equal
sequence numbers with unequal valid records and duplicate source-page keys
reject the cache before root-file writes.

Root-page application uses a durable original/native record first. After HFS
replay, a page equal to original, native, or a bytewise mixture caused by an
interrupted write can be completed. Any other page content is skipped. An apply
I/O failure stops before userspace, preserving the durable repair record.
Journaling protects filesystem metadata; it does not itself promise atomic file
content writes. Actual sudden power removal has not been tested on hardware.

Original pages stay in the cache. A maintenance BOOT with
`b2l_cache_restore=1` restores them before init and disables learning. The
single-core 0031 BOOT remains the retained rollback artifact. A new root UUID or
incompatible cache version requires explicit cache reinitialization; old records
are not silently adopted for another installation. A full cache stops accepting
new pages; it never evicts the only retained original of an applied page.

## One-time provisioning and verification

`scripts/init-native-cache.py` creates a new local `b2l-cache.bin` from a saved
4096-byte root volume header. It never accesses a device. Allocate the complete
file on the otherwise-unused SYSTEM HFS volume and cleanly unmount it. The
loader rejects sparse/fragmented/overflow extents and a pending SYSTEM journal;
USERDATA can retain its pending journal because only XNU writes it after replay.

Run `scripts/test-native-cache.sh [local-system-image]` for format/conversion,
SHA-256, stale-page, every torn-record prefix, every interrupted root-page prefix
and optional real-HFS placement checks. `scripts/test-loader.sh` verifies DT and
RAM constraints; `scripts/test-arm64e.sh <prepared-xnu>` preserves emulator and
mixed-page pager regression coverage.
`scripts/check-native-cache-codegen.py <object-root>` also verifies actual scalar
compiler arguments and rejects SIMD/FP in all cache/ARM64e object disassembly.
These checks are distinct from hardware
proof of the first learned record and the subsequent pre-init application.

Runtime lines:

- `B2L cache: staged 128 MiB read-only` (EFI).
- `B2L NCACHE: bounded SYSTEM file storage ready` (native transport).
- `B2L NCACHE: pre-init apply applied=... already=... skipped=...`.
- `B2L NCACHE: automatic learning enabled`.
- `B2L NCACHE: queued=... learned=... applied=... ... errors=...` (~30 seconds).

At least one learned record and a later boot with `applied`/`already` increasing
are required to call the complete automatic cycle hardware-verified. A reduction
in traps or improved startup time must be measured separately; there is no
assumption that caching fixes missing graphics, input, services or SMP support.
