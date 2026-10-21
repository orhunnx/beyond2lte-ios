# Build 0004: full boot seed for XNU 12377

## Confirmed failure

Build 0003 physically reached C entry, platform initialization and arm_vm_init.
Its early console reported that the ARM64 PRNG requested 256 seed bytes but the
loader supplied 64. The pinned public XNU source requests four SHA-512 digest
lengths on ARM64 and treats a shorter seed as fatal.

## Fix

The EFI RNG now requests 256 bytes. Without EFI RNG, the BOOT packager supplies
256 bytes directly from the host operating system using os.urandom. Exact-size
seed files are also supported. The Apple DT random-seed property carries all
256 bytes. Kernel entropy checks are unchanged, and the 0003 kernel is reused
byte for byte. No seed expansion, repeated data or zero padding is used.

BOOT payload v2 uses magic B2LXNU02, version 2 and a 320-byte header. Kernel
length and offset stay at bytes 16 and 24; the kernel offset is now 320.
The 256-byte seed occupies bytes 32 through 287, followed by 32 reserved bytes.
Both loader and verifier reject legacy 64-byte payloads with a repackage error.
Android BOOT remains version 1 with an empty ramdisk.

## Checks and package

EFI and the external 4 MiB firmware profile rebuilt successfully. Host tests
passed for the full DT property and all buffer capacities, payload seed contents,
legacy rejection, truncation, offsets and overflow. All 16 Python tests passed,
as did the ASAN/UBSAN Mach-O, ADT, RAM arena and framebuffer-panel tests.
The exact rebuilt EFI loader was found in the compressed firmware volume.

Image: ignored out/0004/boot.img, 22,518,800 bytes. Confirmed BOOT capacity:
57,671,680 bytes. The kernel hash matches 0003. The reference physical guard map
is byte-identical to the existing firmware. No proprietary inputs, seed bytes,
raw logs, photographs or device-unique identifiers are committed.

## Next device evidence

Confirm progress past stage 7 and inspect the next early console line or panic.
VM initialization and actual timer interrupt delivery remain unverified.

## Installation

The BOOT-only update completed with a full backup of the preceding image.
The original pre-0001 backup was preserved. The 22,518,800-byte prefix matched
on readback, then the device was rebooted. The local receipt and identifiers
remain ignored. Progress beyond the previously observed seed panic is pending
a new screen report.
