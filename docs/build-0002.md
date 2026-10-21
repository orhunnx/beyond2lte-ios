# Build 0002: guarded RAM and visible kernel handoff

## Device evidence from build 0001

The device photograph shows the embedded EFI loader, a 512 MiB arena, the
1440×3040 GOP mode and the message before ExitBootServices. It does not establish
XNU entry. The exact stopping point and root cause are still unconfirmed.

## Changes

- Choose the largest eligible contiguous EFI conventional-memory descriptor,
  aligned to 2 MiB, with 32 MiB of EFI headroom. Accept 1–2 GiB; report and use
  the actual allocated size. Fail closed if no qualifying allocation succeeds.
- Preserve the reference firmware MemoryMapLib byte for byte. Existing RAM,
  firmware and MMIO guards remain unchanged. This is not a 6–8 GiB memory map.
- Display six large labelled stages: MAP READY, EBS RETURN, HANDOFF, CACHE CLEAN,
  MMU OFF and KERNEL JUMP. These require no EFI calls after ExitBootServices.
- Clean the kernel, loader and stack ranges by VA before disabling MMU/cache.
  Avoid global set/way operations and cache geometry selector registers.
- Bound the firmware GICv2 EBS drain to 4096 acknowledgements, preserve the full
  raw SGI token in EOIR, and use combined EOI mode. No out-of-window DIR access
  or MMIO mapping extension is introduced.
- Skip the optional B2L OSLAR write. Paint an enlarged XNU entry marker before
  system-register access. Scale the framebuffer console 3× at native resolution,
  including cursor and scrolling, and print a C VM initialization banner.

These changes address identified hazards and improve observation; they do not
prove which hazard stopped build 0001. CPU count remains one.

## Build and checks

The DEVELOPMENT_ARM64_B2L standalone kernel and dSYM, ARM64 EFI loader and
4 MiB external firmware profile built successfully on 2026-10-21. Final BOOT
size is 22,499,912 bytes, below the confirmed 57,671,680-byte target capacity.
The image is `out/0002/boot.img`; exact hashes are in its ignored manifest.

Mach-O, device-tree ABI, RAM descriptor selection and progress-raster host tests
passed under AddressSanitizer and UndefinedBehaviorSanitizer. Extracted kernel
console functions passed scaled glyph/cursor and overlapping-scroll tests.
All 11 Python tests passed, including compiled mock GIC MMIO checks. The XNU
patch applies to the pristine pinned index and matches the prepared source.
Packaging verified the exact loader inside the compressed firmware volume and
validated the Android header and bounded XNU trailer.

## Installation boundary and next evidence

The original full BOOT backup from before build 0001 remains under ignored
`out/backups/2026-10-21/`; it must never be overwritten. Only the confirmed
beyond2lte BOOT partition may be updated under the user's existing approval.
No SYSTEM, USERDATA, recovery or GPT change is needed.

Record the final coloured stage, the XNU entry marker and any kernel console
output. A successful KERNEL JUMP stage establishes only that the branch is
about to execute. The marker establishes assembly entry; the C banner and
real timer interrupt proof are separate milestones. No root filesystem is
included, so a later root-device wait is expected.

## Device result

The BOOT write completed after a full backup of the preceding image; the
original pre-0001 backup was preserved. The exact 22,499,912-byte image prefix
matched on readback and the device was rebooted. The local receipt is ignored.

The user photograph shows all six handoff stages green, including KERNEL JUMP.
This confirms ExitBootServices returned and the MMU-off path reached the final
branch preparation. The first photograph crops the bottom of the panel. The user subsequently
confirmed the separate green rectangle at the bottom of the screen. This is
painted by XNU before its first system-register access, establishing physical
assembly entry. C initialization and interrupt delivery remain unverified.
The next iteration adds the early framebuffer text path used in the A5
reference port.
The photograph itself and device identifiers are not committed.
