# Build 0024: executable shared-cache leaf validation

## Observed failure

Build 0023 passes unsigned shared-cache registration and reaches dyld helper dispatch. The device reports `0xd73f0910` (`BLRAA x8, x16`) at a dyld address whose 16 KiB-aligned image offset matches `0x4f390`. This opcode is already handled by both decoder paths. The previous wrapper checks execute permission using `vm_map_check_protection`, which does not descend into constant submaps. XNU deliberately maps the shared-cache parent read-only (`vm_shared_region.c`) while executable leaves reside inside its constant submap (`vm_map_lock_internal.h`). The wrapper therefore rejects a valid call into the cache. The specific register target was not captured in the photo; runtime continuation after this correction must confirm this diagnosis.

## Change

The wrapper uses `vm_map_region_recurse_64` with nonzero maximum depth and short region info to inspect the leaf. It requires successful lookup, the expected output size, a non-submap leaf, current execute permission, and containment of the entire four-byte instruction. It rejects returned next regions covering a hole, truncated extents, unaligned/out-of-user-range addresses, and unreadable instructions. Existing fault-recoverable instruction copyin and transactional saved-state updates remain intact. Short info avoids walking resident pages on every trap. No PAC bits are guessed or stripped from arbitrary targets.

The core decoder and assembly are unchanged. A failure now also reports decoder result and x8/x16/LR operands to distinguish future target faults. No memory permissions, RAM layout, disk contents or security-policy gates are broadened.

## Verification

The shipped wrapper is compiled against mocked VM/copyio interfaces with ASan/UBSan in both the C decoder and native ARM64 assembly configurations. The exact failing opcode passes with an executable constant-submap leaf; negative cases preserve all saved state for non-executable leaves, query errors, unresolved submaps, malformed counts, region holes/short extents and kernel targets. Native XNU compilation and package bounds pass, and stripped/unstripped/dSYM UUIDs match. BOOT installation/readback is recorded separately in ignored output. Device execution is required before claiming launchd startup.
