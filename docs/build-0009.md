# Build 0009: built-in open-source pthread kernel provider

## Confirmed failure

Build 0008 establishes boot CPU registration, real timer and self-SGI delivery,
and timed scheduler wakeups on the physical S10+. It then reaches BSD's
pthread_init and panics because the provider function table is NULL.

The A5/SWEET 0019 staging/integration and A5 0102 patch solve the same component
boundary by linking the public libpthread kernel implementation and registering
it before BSD initialization. The B2L implementation follows that architecture
with the release-paired current provider, not the older A5 source version.

## Scope

This change supplies an actual kernel-side pthread implementation. It does not
skip pthread_init or replace threading and synchronization functions with
success-returning stubs. The firmware, reserved memory guards and managed RAM
arena are unchanged. A root filesystem and iOS userspace remain later work.

## Implementation and host verification

The provider is Apple OSS libpthread-539.100.4, pinned to
`1f4f5265b319111142f1bf3a27d4484ef5a98314`. Its real kern_init, kern_support
and kern_synch sources compile into the kernel. Registration runs at
EARLY_BOOT/SECOND before lockdown; BSD still calls the actual _pthread_init.
The official version-0 function table and legacy registration path are retained.
All 61 callbacks referenced by the provider exist in the current XNU table.

The B2L kernel reserves a 56-byte, 8-byte-aligned wait element. Compile-time
assertions verify provider size, alignment and the trailing uthread pointer at
offset 48. Source staging pins and verifies the dependency, performs only
explicit ABI/address-type adaptations and refuses modified staged input.
Downloaded sources and SDK headers remain external build inputs.

The standalone kernel/dSYM build succeeded. The kernel is 17,968,416 bytes;
the verified BOOT image is 22,541,920 bytes. The complete tracked patch applies
against the pristine pinned XNU index, and prepared overlays match the build.
Packaging also verified the exact EFI loader within the reused firmware.

ASan/UBSan host tests run actual provider registration, start/current-thread
functions and _pthread_init initialization sequence with hosted kernel hooks.
They verify wait-element storage, startup order, callback names, the official
table version, missing/duplicate registration failures and rejection of altered
staged source. These are not physical pthread syscall or scheduler tests.
Independent review confirmed ABI layout and actual linked provider symbols.

## Installation and device boundary

The authorized BOOT-only update completed after a full verified backup of the
previous BOOT. The original backup remains intact. Exact image-prefix readback
matched, then the device was rebooted. Raw receipts and identifiers remain
local and ignored.

The next device report must show progress past
pthread_init, with `B2L pthread: libpthread-539.100.4 registered` visible.
No root filesystem is supplied, so a later root-device wait is expected.
