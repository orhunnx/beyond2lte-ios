# 0098 runtime findings

Read-only recovery export completed with no errors and unchanged HFS header.
The persistent kernel region is epoch 59, with 1,029 valid records and 27
producer drops. Its tail ends before the on-screen panic; it cannot supply a
complete kernel backtrace for that panic. Native trace decoding selected boot
`8EB4F693-BE16-488C-8D81-49DD02842E05` and yielded 39,738 events. The owned
helper crash report has the same boot UUID.

## Service-cache handoff progresses

The v2 cache is 2,279,863 bytes. The native loader selects it, launchd starts
services, and launchd records spawning the owned bootstrap as pid 70. This
establishes progress past the 0096/0097 `No service cache` exit.

## Native device artwork profile regenerated

The original 8,029-byte profile remains in its preserved backup. The new native
MobileGestalt cache is 8,250 bytes with 38 CacheExtra entries. Its artwork answer
now supplies idiom phone, subtype 2868, scale 3, sRGB, product description
iPhone 16 Pro Max and GLES2,0 feature/fallback values. This verifies native
profile regeneration; visible icon correctness is a separate runtime result.

## Why applications remain absent

There are zero installed application bundles and no bootstrap attempt receipt.
The helper's log is empty. Its crash report identifies EXC_BAD_INSTRUCTION,
SIGILL, instruction `0xd65f0fff` in the arm64e dyld's
`dyld4::fixupPage64`, before main. The executable itself is arm64. Existing
compatibility emulation intentionally requires an arm64e main process subtype,
so this mixed main/dyld combination does not enter emulation. Build 0099 compiles
the owned helper for arm64e instead of widening emulation to other processes.

## Settings panic

The owner's screen shows `IOGMD: not wired for the IODMACommand` on MPIDR 0x101.
The matching XNU branch checks `_wireCount` in virtual descriptor segment walking.
Native IOUserClient creates OOL descriptors withAddressRange and does not prepare
them before invoking the driver. Our IOSurface, KeyStore, watchdog and AMFI
adapters had direct readBytes/writeBytes calls without balanced prepare/complete.
Those byte-copy methods walk physical segments and reach the observed check.

0099 corrects these missing preconditions at the owned adapter call sites.
The persistent log does not include a panic backtrace identifying which call
site triggered the screenshot. Runtime must confirm Settings no longer hits
this invariant and distinguish any later graphics errors.
