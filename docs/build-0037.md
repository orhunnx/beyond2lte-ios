# Build 0037: SpringBoard platform status and watchdog service

## Verified 0036 crash evidence

Recovery yields epoch 9 with 89 valid records, 145396 text bytes, sequence zero,
no missing positions and zero producer drops. The final saved line is incomplete.
FIPS reports all tests passed and aggregate true. Early boot completes.
The new backtraces work, although concurrent console producers interleave lines.

The first SpringBoard trace contains 12 frames with no unwind error. Its chain
runs through dyld initializers, CoverSheet, __assert_rtn, abort and pthread_kill.
Four independent logged instructions uniquely determine the shared-cache slide.
Disassembly of the CoverSheet caller resolves SecTaskCreateFromSelf and
SecTaskGetCodeSignStatus. Its test requires `(flags & 0xfc000001) == 0x04000001`,
with an internal-content alternative. The assertion identifies SBStaticInitializer,
StaticInitializer.m line 39 and the condition `platformBinary`. This establishes
a platform-status contract failure rather than an unsupported ARM instruction
at this crash. Do not infer broader emulator correctness from it.

usermanagerd separately reports `Daemon failed to load persona manifest.`
UserEventAgent reports an event-stream error with ESRCH. securityd and
ManagedSettingsAgent reach Swift assertions. These remain separate unresolved
issues; removing the SpringBoard initializer barrier does not establish UI boot.

watchdogd explicitly fails to discover its IOWatchdog service, then traps.
The A5/SWEET donor supplies this service in patches 0040 and 0045, but B2L did
not. Local watchdogd also uses selectors 6, 7 and 8 absent from the older donor
comment, as well as IOPMUBootStage and IOPMUBootAppName property writes.

## Changes

Board patch 0025 adds query-only CS_VALID compatibility inside CS_OPS_STATUS
when the existing built-in rootfs development platform classification succeeds
and the AMFI development provider is active. Native process flags, CS_SIGNED,
cs_blob state, signed VM pages and cryptographic verification are not changed.
This is development acceptance metadata and does not verify an Apple signature.
Existing access checks and explicit platform-state clearing remain in effect.

The built-in IOWatchdog and type-1 user client register only while the existing
unsigned ARM64e development mode is enabled. It exposes IOPMUPrimary, accepts
bounded boot-stage/application properties, and supports only the observed
selector shapes 0 through 9. Unknown selectors, async calls, malformed buffer
counts and oversized structures fail. Scalar queries and the two-byte status
query report zero; check-ins are accepted without arming a hardware watchdog.
**This PoC provides no hardware watchdog reset protection.**

## Verification and installation

The real adapter passes ASan/UBSan tests covering observed calls, guarded output
buffers, descriptor write failure and malformed requests. The actual 24-to-25
patch sequence passes with idempotence and refusal of local edits. The full
DEVELOPMENT kernel builds; watchdog machine code has no FP/SIMD operands.
Matching kernel/unstripped/dSYM UUIDs are retained privately. Existing EFI and
loader are reused and the embedded loader is verified during packaging.

BOOT size: 23185944 bytes. SHA-256:
`7fff6bf78f2c324e9962a61fcaabdb2de9bd028effb7fdd3c87855b4332904ae`.
The installed 0036 image prefix was verified before writing; full 0037 prefix
readback matches after installation. Reboot was requested. No SYSTEM/USERDATA
image was rewritten. Service registration, passing the SpringBoard initializer
and later graphical startup require the next device observation.

Raw logs, Apple code and addresses remain ignored local artifacts.

## Recovered device result

The next recovery capture confirms IOWatchdog registration and successful
selector 0. The previous SpringBoard and watchdog failures are not observed;
a graphical session remains unverified. usermanagerd repeatedly fails with
ENOENT for its missing persona manifest. [0038](build-0038.md) seeds that
metadata after journal replay without replacing an existing manifest.
