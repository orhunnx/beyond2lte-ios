# beyond2lte-ios

Boot Apple's open source XNU kernel on the Samsung Galaxy S10+ SM-G975F
(`beyond2lte`, abbreviated **B2L**, Exynos 9820).

The first milestone is kernel entry, framebuffer console output and real timer
interrupt delivery on the device. iOS userspace and SpringBoard are later
milestones. An IPSW is not required for this initial kernel boot image.

## Boot chain

`Samsung S-Boot → BootShim → beyond2lte UEFI → embedded XnuLoader → XNU`

The existing [beyond2lte UEFI port](https://github.com/orhunnx/beyond2lte-uefi)
is an external firmware dependency. This repository contains the XNU board
changes, standalone EFI applications and packaging scripts. ACPI and Windows
implementation files are not copied here. The XNU firmware profile omits ACPI
and selects the XNU loader as its default boot application. U-Boot is not used.

## Source baseline

- [XNU `rel/xnu-12377`](https://github.com/apple-oss-distributions/xnu/tree/rel/xnu-12377),
  pinned to `ac9718fb1af618d5ce8678d0dc6e8a58f252216f`.
- Generic ARMv8-A with crypto instructions, 4 KiB pages, one boot CPU.
- GICv2 at `0x10101000` / `0x10102000`; virtual timer INTID 27.
- Actual architected counter frequency read at runtime; expected 26 MHz.
- Apple PAC, BTI, SPTM and PPL are not used by this initial B2L target.

## Build and package status

The standalone ARM64 kernel, debugging symbols, EFI loader and external 4 MiB
firmware profile build locally. Packaging produces a single Android v1
`boot.img` with the firmware and a bounded XNU payload trailer. The embedded
loader reads that payload through the existing firmware UFS service from GPT
partition `BOOT`; no separate ESP or SYSTEM/USERDATA transfer is required.

**Physical device proof now covers XNU entry, its own framebuffer console,
VM bootstrap, boot CPU registration, real timer and self-SGI interrupts, and
timed scheduler wakeup. Build 0008 reaches later BSD startup, where the absent
AES provider used by TCP initialization is the current blocker.**
[Build 0004](docs/build-0004.md) supplies the required 256-byte seed; its
device result identified the next DT contract gap. [Build 0005](docs/build-0005.md)
adds the required /chosen/memory-map and /defaults containers.
[Build 0006](docs/build-0006.md) adds allocator RNG and real kernel PRNG startup;
its device test passes PRNG registration and exposes the console lifecycle gap.
[Build 0007](docs/build-0007.md) restores initialized console locks and the real
display-control backend while preserving early text. Its device test passes
console attachment and reaches the missing Image4 provider.
[Build 0008](docs/build-0008.md) adapts the A5/SWEET 0015 unavailable-provider
handling to the current trust-cache APIs. Its BOOT-only installation is verified;
the device photograph confirms progress into BSD startup and real IRQ proof.
[Build 0009](docs/build-0009.md) links and registers the matching open-source
libpthread kernel provider. Its device test passes into network initialization;
the next failure identifies the absent AES mode provider.
BOOT writes were backed up and readback-verified.
[Build 0002](docs/build-0002.md) uses a dynamic 1–2 GiB RAM arena within the
unchanged firmware memory guards. [Build 0003](docs/build-0003.md) adds XNU-owned
text from assembly entry through C and the normal printf/kprintf console.
It does not contain an iOS root filesystem. Progress after the scheduler may
stop while looking for a root device; that is separate from initial kernel
entry and interrupt bring-up.

Local artifacts are placed under ignored `out/` directories. Firmware binaries,
SDKs, KDKs, IPSWs and local upstream source checkouts are not committed.

## Build workflow

1. Fetch the pinned source with `scripts/fetch-xnu.sh`.
2. Apply the board changes with `scripts/prepare-xnu.sh`.
3. Prepare external dependencies described in [build notes](docs/build.md),
   then run `scripts/build-kernel.sh`.
4. Build the standalone EFI applications and external firmware profile using
   `scripts/build-boot-firmware.sh` in a disposable Mu work tree.
5. Create the image with `scripts/package-boot.py` as described in
   [packaging](docs/packaging.md).

## Verification and device boundary

```sh
scripts/test-loader.sh
scripts/inspect-kernel.sh /path/to/mach.development.b2l
python3 scripts/package-boot.py --verify /path/to/boot.img
```

Host checks cover malformed Mach-O input, Apple device-tree bounds, the exact
XNU boot ABI and BOOT container arithmetic. Packaging also verifies that the
actual compressed firmware volume contains the selected EFI loader.

No phone partition is written by these build/package commands. The user's
explicit approval is required before an ADB BOOT write. The connected device
must first be verified as beyond2lte and its real BOOT capacity checked.

See [the boot contract](docs/boot-contract.md),
[EFI handoff](docs/efi-xnu-loader.md) and
[runtime milestones](docs/runtime-bringup.md) for execution details and the
screen output needed to establish a successful device test.
