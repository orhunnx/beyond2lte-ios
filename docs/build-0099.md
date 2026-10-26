# Build 0099: prepared IOKit copies and an arm64e migration helper

## IOKit descriptor contract

New B2LMemoryIO.h bounds the entire offset/count range before preparing a
descriptor. Reads prepare/complete with kIODirectionOut; writes use
kIODirectionIn. A failed prepare prevents copying and does not call complete.
Every successful prepare has one complete, including short-copy paths. A failed
complete reports failure to the caller. Existing prepared descriptors retain
their prior wire count through this balanced additional reference.

The IOSurface create/metadata/reply paths and the existing KeyStore, AMFI and
watchdog byte-copy adapters use these helpers. Native XNU's invariant panic and
IOUserClient are unchanged. ABI shapes, output bounds and service policies
remain as before. See [0098 evidence](0098-userspace-findings.md).

## Owned migration helper

The helper is now compiled for arm64e, and generation checks the actual Mach-O
CPU subtype is 0x80000002. This lets its arm64e system dyld use the existing
process-gated compatibility service. The executable is 72,480 bytes; SHA256:
`e76d339138195463e2c36ef7d7815d599c1b7c36a616853d9d6b4c85ac44eca8`.

Exclusive publication uses new owned executable/job paths ending in v2. The
separate selected launch cache ends in v3, retains the original 2,279,863-byte
handoff length and 690 original jobs, and adds just the v2 job. Old artifacts
are preserved and are not selected by the new derived cache.

The native migration calls and entitlements are unchanged. The original v1
attempt receipt and lock paths are deliberately retained across revisions, so
a prior migration attempt is not reset by this executable update. In 0098 the
helper died before main and no attempt receipt was created. The new job's log
path is /private/var/log/b2l-system-app-bootstrap-v2.log.

## Verification

The dedicated actual-code descriptor test passed ASan/UBSan with initially
unprepared and already prepared descriptors, failed prepare/complete, short
copies, null inputs, overflow, range rejection and balanced pin lifetimes.
Affected actual IOSurface metadata/lifetime/limits, AMFI, KeyStore and watchdog
adapter tests passed. Launch-cache handoff, argument copying and bounded
payload-writer tests passed. The production kernel compiled and linked.

Native XPC parsing accepts the actual generated cache and verifies original
object equality after removing the single added job. Scalar code-generation
checks passed; the kernel RCpc audit found zero unsupported instructions.
Firmware and EFI loader are byte-identical to 0095.

## Installation and pending runtime

Only S10+ BOOT was written, after product/root/alias/capacity checks, a full
57,671,680-byte backup and exact previous-0098 prefix verification. Image size
25,896,280 bytes; exact readback SHA256:
`6c1b52d25932155e191ac498b856db1888470ab42b7e9e22279daa7670b5f57a`.
Reboot returned successfully. Private symbols, backup, generated payload and
flash receipt remain in out/0099.

Installation is verified. Settings stability, helper entry into main, native
migration result and actual installed applications require a fresh run. The
source fixes and tests do not establish those device results.
