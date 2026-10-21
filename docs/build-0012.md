# Build 0012: native read-only UFS access

## Starting point and scope

Build 0011 reaches the Apple_HFS root-device wait without a panic. The next
milestone is real kernel-owned disk access after ExitBootServices. The working
open-source beyond2lte UEFI UfsDxe implementation is the hardware reference;
its Samsung command layout and inherited calibrated link are used, rather than
calling EFI services from XNU or treating a RAM image as a disk.

This update is limited to native read-only transport and bounded BSD disk
access. It does not format SYSTEM, install HFS or select an empty filesystem
as the root. The original Android BOOT and SYSTEM backups are retained once;
no additional per-build backup is required.

## Evidence boundary

Recovery supplied a read-only reference with 4096-byte logical blocks and valid
GPT header/table CRCs. The raw sectors, unit layout and identifiers remain local
and ignored. A kernel build and host tests cannot establish that native UFS
works on the phone. The device must print successful native capacity, GPT CRC
and bounded partition read results before this milestone is called verified.

## Implementation and host verification

The native transport inherits the calibrated link, validates the host and its
Samsung descriptor mode, masks controller interrupts and installs fresh,
physically contiguous kernel DMA lists and a bounded bounce buffer. It polls
slot 0 with task tag 0, uses bounded deadlines, and checks OCS plus UPIU
transaction, LUN, tag, status and residual. On an unfinished failed transaction,
DMA memory stays pinned and further commands are disabled. There is no storage
write opcode or public write interface; no PHY, clock, power or HCE reset occurs.

The EFI loader passes the actual selected BOOT handle's validated Exynos vendor
LUN path in the Apple device tree. Missing or malformed metadata never guesses a
LUN. The BSD adapter exposes read-only whole-LU and validated partition nodes;
all views enforce geometry and extent bounds. Main and backup GPT headers and
arrays must agree and pass CRC checks before partition nodes are created.
SYSTEM/super content probing recognizes Android LP geometry from the actual
signature, independently of its GPT label. Root matching is unchanged.

The kernel/dSYM and EFI/embedded firmware builds succeeded. The kernel is
18,020,344 bytes; the validated BOOT image is 22,593,848 bytes. The complete
patch applies to the pristine pinned source, and all overlays match the built
source. The actual firmware volume contains the exact selected new EFI loader.
The physical memory/MMIO guard map is byte-identical to the original reference.

ASan/UBSan tests execute the actual descriptor/polling core with fake MMIO,
checking high DMA addresses, read-only command admission, bad completions,
timeout/wrap and buffer bounds. Device-path and ADT tests validate exact LUN
metadata and malformed/truncated input. Actual-source BSD/GPT tests cover read
views, strategy residuals, rejected writes, ioctls, main/backup CRCs, malformed
geometry and signature classification. The GPT parser also accepted the local
recovery reference sectors and matched the original SYSTEM extent; those raw
sectors and identifiers are not committed. Host checks are not device DMA proof.

## Installation and device boundary

The authorized BOOT-only update completed without taking another backup. The
original Android BOOT and SYSTEM backups remain intact. Exact image-prefix
readback matched, then the device was rebooted. SYSTEM was not written; raw
receipts and identifiers remain local and ignored.

The device test must report successful native capacity,
primary/backup GPT CRC verification and bounded SYSTEM signature reads. Existing
HFS root waiting is expected because HFS/root integration is outside this update.
No SYSTEM write is performed and no new BOOT backup is required.
