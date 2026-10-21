# Build 0008: explicit unavailable trust-cache service

## Confirmed failure and A5 reference

Build 0007's device photo confirms console attachment, PRNG registration and
MAC policy initialization. It then panics at kern_trustcache.c:584 because
Image4 is absent. No raw screen capture is committed.

The A5/SWEET source patch 0015 addresses this same missing-provider startup
contract by leaving trust-cache runtime pointers NULL and rejecting API use.
A5 patch 0106 is a separate later userspace fix: it enables that port's built-in
AMFI and XML-entitlements provider after the first executable reaches
accelerate_entitlement_queries. It is not the remedy for this Image4 startup
panic. This build adapts the former contract to XNU 12377's current API surface.

## Scope and device boundary

This is still an open-source kernel first-boot target, with no iOS root image
or userspace. It must not claim signature validation or trusted cache matches
when their providers do not exist. Device confirmation of boot CPU registration
and the timer/SGI proof line remains pending. Firmware, memory guards and the
RAM arena are unchanged.

## Implementation and host verification

B2L records runtime readiness only after a complete provider is initialized.
Absent or stale providers leave both runtime pointers NULL. All eight public
APIs reject unavailable calls before Image4, AMFI, entitlement, device-tree or
lock dispatch. Output query tokens, REM permissions and capabilities are cleared.
The static loader records its initialization without parsing a cache when the
runtime is unavailable. No fake provider table or trusted match is registered.
The original complete-provider path remains available.

The standalone kernel/dSYM build succeeded. The kernel is 17,947,208 bytes;
the verified BOOT image is 22,520,712 bytes. The same firmware and embedded EFI
loader are reused. The full patch applies to the pristine pinned source index;
the prepared overlays match the built source.

ASan/UBSan exercises the actual entry guards before initialization, with absent,
stale and partial providers, each missing callback, cleared outputs and poisoned
input pointers. A complete provider fixture passes the availability gate.
These tests establish dispatch behavior, not cryptographic validation or physical
execution. The BOOT package layout and exact embedded loader were also verified.

## Installation

The authorized BOOT-only update completed after a full verified backup of the
previous BOOT. The original backup remains intact. Exact image-prefix readback
matched, then the device was rebooted. Identifiers and raw installation receipts
remain local and ignored. The subsequent screen report is recorded below.

## Device result

The user's photograph establishes the first interrupt milestone on physical
hardware: the boot CPU reports MPIDR 0, EL1, IRQs enabled and 1/1 CPU initialized.
Three timed reporter lines show timer counts 43, 45 and 47 with SGI1 count 1 and
`scheduler awake`. This proves real timer interrupt delivery, self-SGI delivery
and scheduler wakeup. Platform expert registration and later BSD startup follow.
The trust-cache provider panic is passed with the explicit unavailable message.

The next failure is `pthread kernel extension not loaded (function table is
NULL)` in pthread_shims.c:319. The open-source libpthread kernel provider must
be linked and registered before BSD pthread initialization. This is a separate
missing software component, not a failed CPU/interrupt test. No root filesystem
or iOS userspace is running yet.
