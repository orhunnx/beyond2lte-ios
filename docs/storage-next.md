# Native storage and HFS root: remaining work

## Verified starting point

Build 0011 reaches XNU's generic Apple_HFS IOMedia root-device wait without a
panic. This establishes the first kernel/CPU/IRQ milestone; it does not establish
that a disk or HFS filesystem driver is installed. The existing EFI can read
UFS before ExitBootServices, but XNU cannot call those expired boot services.

## Native UFS path

Build 0012 implements the read-only native transport, BSD devices and strict GPT
validation described below. Its device photograph confirms native capacity, BSD devices, main/backup GPT
CRC verification and the Android LP SYSTEM signature. Physical native UFS reads
are verified. HFS integration is not included in this kernel.

The existing open-source Samsung UfsDxe implementation provides a hardware
reference. A native read-only driver can attempt takeover of the inherited live
link without cold PHY calibration. It must validate idle request/task doorbells,
64-bit addressing support and live host state before rebinding request lists to
new kernel-owned, aligned DMA descriptors and a bounded bounce buffer. Firmware
pointers or EFI functions must not be used after handoff.

Samsung descriptor offset/length quirks, cache maintenance, completion status,
UPIU tag/status/residual checks and bounded timeouts must be implemented from the
actual reference. The LUN must come from actual discovery or the firmware's real
device path; it must not be guessed. Sector geometry must translate correctly
between UFS logical blocks and BSD's 512-byte buffer block numbering.

A native BSD block/character device with real strategy, read and geometry ioctls
can support an explicit root selector without porting the entire IOStorageFamily.
The alternative is a real IOMedia/IOBlockStorageDriver/IOStorageFamily stack.
Neither approach should claim a memory-backed image is native disk access.

## HFS path

The public release-paired HFS source is hfs-715.120.4, pinned to
`124b970fd5390f1c2cd6f985478616a04f6eeb1f`. Build 0013 adds its explicit built-in
registration path and an unencrypted-root feature configuration. Its
normal iPhoneOS kext configuration assumes additional protection/AppleKeyStore
services absent from this first-boot kernel. Source availability is not a
successful build or filesystem mount.

Build 0013 supplies direct native SYSTEM selection and optional IOStorageFamily
handling for this BSD path. Its explicit development root policy accepts the
unencrypted, unsealed HFS root without claiming Apple authentication. The real
MD5 implementation required by HFS volume UUIDs is included. Physical root-mount
and write/flush proof remain a device test. Empty HFS supplies no init or userspace.
See [HFS integration](hfs-provider.md) and [build 0013](build-0013.md).

## Disk boundary

Preserve the original Android SYSTEM container once, before reuse. The recovery
aliases SYSTEM and super point at the same physical extent in the current setup;
a later write must explicitly account for the dynamic-container scope. Backups,
raw layout and identifiers stay local and ignored. No SYSTEM formatting is
needed to establish the already verified kernel-boot milestone.

The original Android SYSTEM container has now been backed up once, with its
full length and SHA-256 compared against the device. This original backup is retained for future trials. The subsequent authorized
empty HFS+ SYSTEM image is documented in [SYSTEM preparation](system-hfs.md);
filesystem preparation does not establish a mounted XNU root.
The original Android BOOT backup is also retained; per-build BOOT backups are
no longer part of the authorized update procedure.
