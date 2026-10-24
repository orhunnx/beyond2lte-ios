# Build0077: serialize secondary startup and capture low-entry identity

## New device evidence

The second supplied0076 frame shows CPU6/firmware0x100 CPU_ON returning0,
low-stage1, secondary-stage0. This proves that the first M4 reached the low
trampoline, not that it reached the kernel identity gate or C. CPU0 then starts
CPU7/firmware0x101 before CPU6's initialization is acknowledged. The final
frame ends in that call; it does not prove its final return or reset cause.

The recovery SYSTEM region copied under out/logs/0076-recovery is byte-for-byte
identical to out/logs/0075-before-m4. It is epoch43's prior run;0076 stopped
before the persistent log writer initialized. It must not be presented as an
on-device0076 panic log. Recovery's reset-reason value alone is not attributed
to this XNU attempt because the user subsequently entered recovery.

## Changes

SecondaryEntry ABI2 keeps the target at byte40 and stage at48. A branch at28
runs additional stackless generic ARM64 code at64, before branching to the
full64-bit high target. It captures CurrentEL, MPIDR_EL1 and MIDR_EL1 at
bytes128/136/144, cleans the capture, then publishes low-stage2. Target/context
semantics and the existing16KiB firmware-owned allocation are preserved.
The code blob is152 bytes; all immutable code/header words are validated by
the shared template checker. No reserved region or new partition is changed.

After each registerProcessor return, CPU0 waits up to5s for that core's
initCPU flag, timer interrupt and self-SGI readiness. Only then can the next
CPU_ON occur. This retains each low-entry identity until it has been consumed
and avoids overlapping M4 startup in the next experiment. Every stage change
and at most once per second emits B2L SMP WAIT with raw identity. A rejected
affinity or missing C/timer/SGI result panics before the next CPU_ON. This is
an ordering correction and earlier diagnosis; the hardware reset root cause
is not yet established. Eight-CPU topology and final8/8 bidirectional proof
remain required; CPU affinity assumptions are not broadened.

## Checks

- ELF and UEFI COFF assembly are byte-identical; target/stage/identity offsets
  and the full64-bit final BR are checked.
- Real low trampoline executes under QEMU, records actual EL/MPIDR/MIDR,
  preserves context x0 and reaches the supplied target without a stack.
- Every immutable header/code bit mutation fails template validation.
- Actual coordinator body passes40320 provider-arrival permutations with an
  acknowledgement barrier between each registration, plus previous failures.
- Actual wait body passes ready/delayed startup, affinity rejection, missing C,
  missing timer, missing self-SGI and bounded-timeout cases under ASan/UBSan.
- Production kernel and EFI compile successfully. Only BOOT is flashed after
  recovery/product/capacity checks, full backup, expected0076 image prefix and
  exact0077 readback hash. Personalized files remain ignored/private.

Runtime still requires a photo of B2L SMP WAIT or first panic, or the8/8 proof.
