# Build 0074: touch FIFO batching and real MIG zero-output ABI

## Evidence from 0073

Recovery export is complete and read only, under out/logs/0073-recovery.
Current boot A85CD17CC0C54AE4B7A5CB98FDF39089 has 139438 decoded new
Persist/Special events. Kernel log epoch41 has one missing byte position;
producer drops are zero. Seven decoder UUIDText lookup warnings remain, so
this is not a claim of exhaustive native-message coverage.

The driver records 161 down and 161 up events and no three-failure disable.
Its first12 recorded I2C read failures are timeouts. Counts beyond12 are not
available in0073. backboardd attaches authenticated B2LHIDTouch and creates a
SpringBoard touch stream. Two digitizer generation-update warnings do not by
themselves prove the latency source. No per-touch sensor-to-display timing is
available. CoreImage still logs68 render failures with a12x12 destination.

Unlike0072, the prior Sharing discovery/DataMigrator storm is not present in
this snapshot: DataMigrator has282 messages, sharingd187. Nevertheless lsd108
rejects the original extension-point cache at365.681s with code12 and the
actual pinned SHA256 CDHash: the IOKit method returned BadArgument.

XNU is_io_connect_method always assigns MIG scalar/inband output backing
arrays to IOExternalMethodArguments, including when output counts are zero.
The0073 client incorrectly rejected their non-null addresses.0074 checks
counts and descriptor lengths, never dereferences or writes zero-size output
buffers, and preserves selector/type/platform/entitlement/development gates
and the same two exact local CDHash pins. No broader trust policy is added.

## Changes

B2LInput now uses the existing UEFI B2LTouchDxe READ_ONE(0x60), then bounded
READ_ALL(0x61) sequence. A tick consumes up to32 events using two commands,
rather than four individual commands. The first record is delivered before
bulk transfer; a failed bulk cannot swallow that already-consumed record.
At most248 bytes are read into the remaining31 slots of the256-byte stack
buffer. Larger FIFO backlogs are left for the next tick; no clear or reset is
added to discard events. Poll scheduling stays20ms, preserving GPIO debounce.
The existing three consecutive transfer-failure cutoff is retained.

Every10s, bounded INPUT PERF diagnostics show poll/event/update/bulk/failure
counts and maximum FIFO depth, start-to-start gap and callback duration.
Down/up logs include monotonic microseconds. HID dispatch durations above50ms
are logged only for the first16 instances. Queue loss is logged for the first
8 drops and then powers of two. This separates sensor polling/workloop delay,
kernel dispatch blocking and user-client queue overflow in the next run.
Counters do not measure final visible-frame latency.

## Checks and installation

ASan/UBSan executes the actual poll method with the real sec_ts parser:
32-event ordering, press/move/release,63-record backlog, failed bulk preserving
the first event, recovery and three-failure cutoff, and scheduling diagnostics.
AMFI tests now include the real non-null zero-count MIG output shape and
inline backing storage beside descriptor input. Positive output lengths,
malformed input, unknown hashes and caller-gate failures still reject.
Exact original cache/signature provenance and all320 CDHash bit mutations
pass. Production XNU compilation and link pass for all four changed units.

Only BOOT is written after serial/product/recovery/partition/capacity checks,
a full BOOT backup and expected0073 prefix comparison. Exact0074 readback
SHA256 is required before reboot. Personalized image data stays in ignored,
private out/0074; factory MAC is never written to this document or published.
Runtime responsiveness and FIFO/queue behavior still require device testing.
