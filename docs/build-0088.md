# 0088: UFS DMA across owned RAM banks

## Evidence and correction

After 0087 reported eight initialized CPUs, it failed root selection with
`B2L UFS: requested HFS root unavailable: 6`. The recovered 16MiB persistent
region still contained the 0086 epoch 48 log; no new 0087 UFS log was saved.
The exact on-device transport error therefore remains unrecorded.

Static inspection found a concrete regression: b2l_ufs_transport_init rejected
any contiguous DMA allocation outside the original 4024MiB bootstrap arena.
The expanded allocator can return low DDR or extra upper DDR. Such an
allocation returned EFAULT before takeover; the unpublished root later returned
ENXIO (6).

The transport now validates the full 77,824-byte DMA extent against the
EFI-owned range list, retaining per-page physical contiguity checks and
rejecting reservations, holes and overflow before any controller takeover.
A short early line records the validated physical extent. RAM expansion,
eight CPUs, scheduling, firmware and all filesystem/cache formats are retained.

## Verification

The new host regression test executes the production UFS initializer with
mocked kernel/DT/MMIO primitives. Low DDR, extra upper DDR and original primary
RAM reach takeover; reserved DDR, crossing extents, overflow and fragmented
physical pages do not. Existing descriptor/read/write/flush/LUN tests pass.
The real kernel build succeeds and the linked RCpc instruction audit is zero.
Host verification does not establish successful hardware DMA or HFS mount.

## Installation

Only BOOT is targeted, with recovery/product/partition/capacity checks, full
backup, exact 0087 prefix verification and exact new-image readback before
reboot. Personalized artifacts, symbols and receipts remain ignored in out/0088.

0088 was written and read back successfully on the recovery target. The full
57,671,680-byte backup and previous 0087 prefix check passed. The image is
23,518,696 bytes, SHA256:

`c5b5992aa44ebe4f7168ae8a6da963d566049565864ff568c682e720dd11cd08`

Readback matched exactly and the reboot command completed. Hardware UFS DMA,
the actual owned RAM total and HFS root mounting remain pending device proof.

## Recovered runtime

The owner reports a very late lock screen and unresponsive touch. Recovery
export is complete with unchanged source header. Kernel epoch49 contains the
last approximately22,011-34,371 seconds, so the early RAM-total line and first
mount messages were overwritten. The native archive contains1,348,743 decoded
current-boot events (CC4CF21C-E2C2-4434-B7F3-C63744791835) and69 matching crash
reports; SpringBoard/backboardd are not among those reports. This is limited
coverage, not proof that no other failures occurred.

Touch remains enabled; the final input sample records3,143 updates,1,488,864
polls and90 transfer failures. The preserved end-of-run active touch sample
shows a maximum polling gap67,543us and work46,518us. The log contains physical
down/up events and no HID queue-overflow line. Native backboardd matches
B2LHIDTouch as its authenticated built-in main-display digitizer. It also reports
`Failed to create event at index=1 , eventDataMaxSize:132 < eventDataSize:1070767360`.
This identifies malformed event serialization beyond the sensor path.

Native startup is spread across hours: main display initialization, digitizer
matching and SpringBoard setup are present, followed by touch stream creation
initially marked ignoring. A lone initial ignoring state is not proof of a
permanent suppression condition. Repeated service starts and69 crash reports
add workload, but no CPU-time attribution establishes the dominant startup cause.
Crash families: dasd19, audiomxd17, webbookmarksd15, MessagesBlastDoorService13,
AccessibilityUIServer2, amsaccountsd1, mediaparserd1, HeuristicInterpreter1.
