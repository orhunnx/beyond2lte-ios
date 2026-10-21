# Build 0003: XNU-owned early framebuffer text

## Purpose

The user confirmed build 0002's bottom green rectangle, establishing XNU
assembly entry. This build makes later bootstrap progress visible using the
early-console lifecycle from the A5 reference port, adapted to B2L geometry.

## Implementation

- Stackless assembly draws XNU ENTRY, MMU PREP, MMU ON and ENTER C using the
  public XNU iso_font, before normal console facilities exist.
- Geometry comes from boot_args. The physical framebuffer is mapped in the
  bootstrap TTBR0, then given a reserved TTBR1 alias before TTBR0 teardown.
- Both rounded mapping ranges are checked against the existing 20 MiB display
  reservation. No physical RAM/MMIO guard is expanded or made allocatable.
- An allocation-free C renderer prints initialization milestones and early
  panic messages. It wraps lines and uses 3× text at native resolution.
- A framebuffer serial sink is registered before UART/serial-phandle early
  returns. Normal printf and kprintf continue through the same renderer.
- The CPU count remains one and the EFI RAM policy remains 1–2 GiB.

## Local verification

The standalone DEVELOPMENT_ARM64_B2L kernel and debugging symbols built.
The kernel is 17,945,296 bytes; the packaged BOOT is 22,518,608 bytes and fits
the confirmed 57,671,680-byte partition. Existing build 0002 EFI firmware is
reused unchanged, with its exact embedded loader verified by the packager.

The early renderer passed ASAN/UBSAN tests for row wrapping, carriage return,
high-bit glyphs, padded scanout rows, relocation, panic formatting and rejection
of invalid/out-of-reservation geometry. Existing scaled-console tests, Mach-O
inspection, 11 Python tests and package validation also passed.

Source and open source patches are committed. Kernels, firmware, images,
installation receipts, device identifiers and photographs remain outside Git.

## Device boundary

Image: ignored `out/0003/boot.img`; component hashes are in its local manifest.
Device behavior is pending. The first visible XNU text and the last initialization
line are required before claiming C initialization, VM setup or interrupt proof.
Only the authorized BOOT partition may be updated; the original backup remains.
