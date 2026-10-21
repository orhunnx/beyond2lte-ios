# Build 0007: initialize the real console lifecycle

## Confirmed failure

Build 0006 passes allocator RNG startup and registers its HMAC-SHA256 PRNG.
The next photographed panic is an invalid, all-zero ticket lock. Matching the
address against the exact kernel symbols identifies vcputc_lock. The early
B2L text path had bypassed stock console attachment, leaving both control
locks and the backend callbacks uninitialized.

## Implementation

B2L declares the console ticket and progress locks through XNU's standard
LOCKS startup infrastructure. The later legacy initializers are suppressed
for this target to avoid double initialization. PE_init_printf(TRUE) once
again attaches the console. The attachment initializes real vinfo and gc_ops
callbacks through initialize_screen, using the existing validated early GOP
virtual mapping. It creates no second mapping and never frees that permanent
mapping. Initial scanout ownership preserves the diagnostic history through
the first acquire; explicit later controls retain the normal lifecycle.

No lock validation is bypassed. Firmware memory guards, RAM arena, CPU count,
EFI loader and firmware are unchanged.

## Build and host verification

The standalone kernel and dSYM build completed successfully. The kernel is
17,947,320 bytes; the verified BOOT image is 22,520,824 bytes, within the
confirmed 57,671,680-byte BOOT partition. Packaging verified the exact EFI
loader embedded in the reused firmware volume. The complete source patch
applies against the pristine pinned XNU index.

ASan/UBSan tests exercise actual console lifecycle functions, including pause,
resume, disable, enable with allocation and clearing, repeated attachment,
and preservation of early text. Early renderer regression tests also pass.
Independent source review checked lock startup order, callback initialization
and the absence of remapping or freeing the reserved framebuffer alias.

## Device boundary

The authorized BOOT-only update completed after a verified full backup of the
previous BOOT. The original backup remains intact. Exact image-prefix readback
matched and the device was rebooted. Raw receipts and identifiers remain local
and ignored.

The next screen report must establish progress
past console attachment, then boot CPU registration and the dedicated timer
and SGI proof line. A root-device wait is possible without a root filesystem.
