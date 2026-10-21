# Build 0001: initial XNU BOOT image

## Result

A raw Android v1 BOOT image was built locally on 2026-10-21. It is a kernel
bring-up image, not an iOS userspace installation. Physical boot has not yet
been verified.

| Item | Value |
| --- | --- |
| Target | Samsung SM-G975F, beyond2lte, Exynos 9820 |
| XNU pin | `ac9718fb1af618d5ce8678d0dc6e8a58f252216f` |
| External Mu pin | `2147dec93fc293ec85b1814a828816130d6f93b7` |
| Reference platform overlay pin | `48add5bb03ab6f7ee4eb5aa04a30366e6dde6ba3` |
| Firmware FD | 4,194,304 bytes at `0xC9000000` |
| EFI loader | ARM64 EFI application, 17,408 bytes |
| XNU Mach-O | Plain ARM64, 17,926,560 bytes |
| Final BOOT image | 22,499,872 bytes |
| CPU configuration | One firmware boot CPU, 4 KiB pages |

The local output is `out/0001/boot.img`. `manifest.json` records exact component
hashes and image layout. `beyond2lte-xnu-boot.tar` is a distribution archive,
not an Odin package. Binary outputs are excluded from Git.

## Completed checks

- Standalone kernel `build` target and dSYM generation completed successfully.
- Standalone EFI loader and external XNU firmware profile built successfully.
- The Mach-O passed the EFI parser's segment and entry-point checks.
- Mach-O and Apple DT/boot ABI host tests passed with ASAN and UBSAN.
- Six firmware-container parser regression tests passed, including nested
  RAW/FV_IMAGE sections and rejection of truncated or missing loader data.
- The packager decompressed the actual firmware FV and confirmed the exact
  final EFI loader was embedded.
- Final Android header, trailer offsets, lengths and plain ARM64 type checks
  passed. Shell scripts passed syntax checks.
- The full patch applies to the pristine pinned XNU index; the overlay matches
  the built source files.

The firmware FD size and its original physical layout were preserved. The
Android ramdisk is empty. XNU is stored in a BOOT trailer read through firmware
UFS, so this package does not introduce a Samsung ramdisk load address or need
an ESP, SYSTEM or USERDATA write.

## Device installation and pending boot evidence

The device was subsequently connected and verified as beyond2lte / SM-G975F
in recovery with a root ADB shell. Its GPT partition name is lowercase `boot`,
resolved at `/dev/block/sda14`, with a confirmed capacity of 57,671,680 bytes
(55 MiB). The loader accepts the exact labels `BOOT` and `boot`.

The user explicitly approved the BOOT write and then required a backup first.
A full 57,671,680-byte BOOT backup was saved under ignored `out/backups/` and
its SHA-256 matched the device's unchanged BOOT before any flash. The final
package records the confirmed capacity. The firmware launches the embedded
loader directly before cached BootNext/BootOrder can select an older OS.

The approved write has now completed. The 22,499,872-byte image was written
only to `/dev/block/sda14`, and a readback of that exact prefix matched the
packaged image. The device was then rebooted. No SYSTEM, USERDATA, recovery
or partition-table write was performed. The local installation receipt is
`out/0001/flash-receipt.json`.

Observe the entry marker, XNU console, CPU registration
and `B2L IRQ proof` lines described in [runtime milestones](runtime-bringup.md).
A later root-device wait is expected without a kernel storage driver and root
filesystem. Screen or runtime evidence must be captured before claiming a
successful physical kernel boot.
