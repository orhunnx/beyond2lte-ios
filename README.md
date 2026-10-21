# beyond2lte-ios

Boot Apple's open source XNU kernel on the Samsung Galaxy S10+ SM-G975F
(`beyond2lte`, abbreviated **B2L**, Exynos 9820).

The first milestone is kernel entry and early console output on the device.
iOS userspace and SpringBoard are later milestones. An IPSW is not required
for the initial kernel boot experiment.

## Boot chain

`Samsung S-Boot → beyond2lte UEFI → B2L XNU loader → XNU`

The existing [beyond2lte UEFI port](https://github.com/orhunnx/beyond2lte-uefi)
is an external firmware dependency. This repository contains the XNU board
changes and EFI applications. Its scope does not include the firmware's ACPI
or Windows support. U-Boot is not part of this chain.

## Source baseline

- [XNU `rel/xnu-12377`](https://github.com/apple-oss-distributions/xnu/tree/rel/xnu-12377),
  pinned to `ac9718fb1af618d5ce8678d0dc6e8a58f252216f`.
- B2L uses generic ARMv8-A instructions, 4 KiB pages and one boot CPU for the
  initial bring-up. Apple pointer authentication and BTI are not enabled.
- The device reference DT describes GICv2 at `0x10101000` / `0x10102000` and
  architected virtual timer interrupt 27 with a 26 MHz counter.

Use `scripts/fetch-xnu.sh` to fetch the pinned source. Local build products,
Apple SDK/KDK files and firmware images are excluded from version control.

## Implementation status

- `uefi/BootSurvey` builds as an AArch64 EFI application. It reports the CPU
  execution level, GOP framebuffer and conventional EFI memory ranges.
  It does not exit boot services or start XNU.
- `uefi/XnuLoader/MachO.c` validates a plain ARM64 executable, segment bounds
  and its entry point. It rejects ARM64e, dynamic imports and chained fixups.
  Host tests cover malformed images and integer/bounds checks.
- `xnu-overlay` and `patches` provide the initial B2L build configuration and
  changes required to compile this open source kernel with local dependencies.
- The standalone kernel target and debugging symbols build locally. EFI handoff, GICv2 runtime integration and
  device boot verification remain in progress. A linked kernel alone is not
  a flashable or device-tested XNU boot image.

See [the boot contract](docs/boot-contract.md) and
[build notes](docs/build.md) for the current requirements and limitations.

## Verification

```sh
scripts/test-macho.sh
```

Device success requires XNU output after the EFI handoff. EFI compilation,
a Mach-O link and a UEFI splash screen do not establish that result.
