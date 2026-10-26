# 0091: bounded adjacent PACGA pair and sampled wrapper costs

## Why

0090's fresh recovery region is epoch51. Its final intact progress summary at
5141s records37,187,989 traps and explicitly says `pacga=siphash24`. All eight
CPUs and6976MiB of owned RAM are confirmed. The owner still saw no usable UI
after80 minutes. SipHash activation therefore does not establish an effective
startup fix. The same four CoreFoundation PACGA sites remain frequent in
last-PC snapshots; these samples are not a CPU-time profile.

The complete current native archive has126,926 decoded events. Backboardd
receives type11 events from the B2LHIDTouch service; the retained messages no
longer contain the earlier child-event-size decoding error. This proves native
event decoding, not SpringBoard input acceptance. Three current-boot audiomxd
EXC_BREAKPOINT reports remain. Older crash reports are excluded from these
counts. Snapshot progress slots that retain old thread observations are not
proof those processes stopped. The archive and kernel region remain private.

## Change

The loader explicitly enables `b2l_pacga_pair=1` in the existing development
ABI. When an emulated PACGA completes, the wrapper may fetch exactly one
following instruction through fault-recoverable EL0 copyin. Only another
PACGA can be consumed, using the existing production decoder and the same
boot-selected hash/key. Its current PC was already checked as executable by
the first decoder; the second decoder validates its own next PC. A successful
pair uses one exception and three VM queries rather than two exceptions and
four VM queries. A third instruction is never consumed. Branches, loads and
unknown instructions retain their usual execution boundary.

If fetching or emulating the second instruction fails, the first result and
PC+4 are committed; the second instruction executes or faults normally after
return to EL0. Software single-step and any thread with a debug-state context
retain one-instruction execution. Missing/zero/invalid pair opt-in keeps the
previous path. There is no global PC permission cache and no execute-permission
or user-range check bypass. SipHash results and the native-cache disk format
are unchanged.

One out of1024 wrapper entries is timed. `B2L EMU PERF` reports cumulative
successful sample count, wrapper elapsed nanoseconds, VM-query elapsed
nanoseconds, PACGA callback elapsed nanoseconds, extra paired instructions and
pair fallbacks. These are elapsed times, including scheduling and lock waits;
VM timing excludes its copyin, and wrapper timing excludes exception entry
and return. Cold key creation may enter a sample. Relaxed snapshots are best
effort and not a coherent CPU-time profile. `B2L LIVE traps/done` still count
exceptions/wrappers, not instructions; add paired extras when comparing work.
No keys or operands are added to the logs.

## Verification

Actual runtime/decoder tests pass ASan/UBSan and ThreadSanitizer for both
QARMA and SipHash. Coverage includes the observed CF pair,2048 register-alias
pairs per algorithm, SP/XZR and FP/LR operands/destinations, no third command,
non-PACGA next instructions, disabled mode, debug/single-step fallback,
recoverable fetch failure, second next-PC execute denial, complete register
state, CPSR preservation and sampled counters. The existing eight-thread key
initialization and mutex-free warm-loop checks still pass.

Kernel/firmware build, linked RCpc audit, BOOT-only installation and runtime
pair acceptance are recorded separately below when completed. No80-minute
startup improvement is claimed from host tests.

## Installed

The kernel and firmware build succeeded. Actual callback/bridge/SipHash code
passed the scalar machine-code audit and the linked kernel has zero RCpc
instructions. The loader binary contains `b2l_pacga_pair=1`.

Only BOOT was written after recovery/product/partition/capacity verification,
a full57,671,680-byte backup and exact0090-prefix verification. The23,535,256-byte
image and its readback share SHA256:

`2e89f77f3aea5db8f271ad3539e850d1b9a5d261c9684716ed2e9762a9b0e92f`

The reboot command completed. Private backup, image, symbols and receipt are
in ignored out/0091. Fresh runtime `B2L EMU PERF paired=...` evidence, startup
latency and UI input acceptance remain pending. This installation is not proof
that the80-minute startup problem is fixed.

## Runtime follow-up

Epoch52 confirms5,212,652 paired extra instructions. Sampled VM-query elapsed
time is99.55% of wrapper elapsed time, while hash time is about0.0129%.
This is wall time including waits, not CPU utilization. [0092](build-0092.md)
replaces the metadata-gathering permission lookup with an exact range query.
