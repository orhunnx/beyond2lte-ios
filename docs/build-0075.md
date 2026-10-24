# Build 0075: elapsed-time I2C deadlines and immediate FIFO draining

## Evidence

0074 epoch42 has zero missing console positions and zero producer drops.
The maximum input callback is776864us and maximum poll interval797914us.
At280–290s, parser updates remain8 while wire events rise1317 to2149 and
callback work reaches724663us: new HID dispatch is not necessary to reproduce
this long read path. Pending sensor events reach63; the driver remains enabled.
No HID queue-drop log or individual >=50ms HID dispatch is recorded.

B2LInput::message counted20000 iterations with MMIO and IODelay(1), rather
than elapsed time. Runtime latency was therefore not bounded to20ms. The
existing loop also delayed after every consumed byte, even when another FIFO
byte was already available.

## Changes

The transfer captures monotonic microseconds before MMIO setup. It checks
elapsed time both in the outer completion loop and while draining/filling the
FIFO, stopping when its deadline expires. It stalls only when no progress can
be made or when awaiting completion after the final byte. It still requires
TRANSFER_DONE, plus TX empty for writes, and preserves reset/quiesce on failure.

Single-event and command runtime messages retain a20ms budget. Bulk messages
longer than8 bytes use100ms, retaining the existing UEFI/probe transport budget:
0074 successful bulk callbacks already reached approximately47ms, so imposing
20ms on all248-byte messages would risk rejecting legitimate transfers.
Probe initialization remains100ms. A scheduler interruption or stalled MMIO
can overshoot a deadline; on resumption the code exits without spending the
remaining iteration count. This is an elapsed-time polling limit, not a hard
real-time scheduling guarantee.

Failure records now include actual elapsed_us and the selected timeout_us.
Periodic INPUT PERF additionally includes max_transfer_us and cumulative bus_us.
Existing32-record ordering, FIFO backlog behavior, GPIO debounce, parser, HID
routing, AMFI pins and access gates are preserved. The AMFI signature-push
failure and digitizer generation warnings identified in0074 are separate;
this build does not claim to fix them.

## Checks

ASan/UBSan executes the actual production message loop against timed MMIO:
- Ready FIFO drains with no per-byte delay for1,8,16,248 and256 bytes.
- Expensive MMIO counts against the20ms deadline, instead of20000 iterations.
- A750ms scheduling jump exits on resumption without more polling iterations.
- Valid approximately50ms248-byte bulk completes inside its100ms allowance.
- Bulk/no-data and probe/no-data stop at their100ms deadlines.
- Missing completion interrupts time out even if all bytes were received.
- RX guard bytes, exact TX payload, blocked TX and invalid bounds are checked.

The real poll body plus sec_ts parser also passes FIFO ordering,63-event
backlog, partial bulk failure, contact release/cutoff and timer-cadence tests.
The production XNU build is required before packaging. Only BOOT is written
with recovery/product/root/partition/capacity checks, full backup, expected0074
prefix verification and exact readback SHA256 before reboot. Personalized
outputs remain ignored/private under out/0075; no factory WiFi address is
published. Device responsiveness remains a runtime validation step.
