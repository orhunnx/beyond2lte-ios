# BOOT image packaging

The first-boot artifact is a single raw Android v1 `boot.img`. It contains the
existing external beyond2lte UEFI firmware, an embedded `XnuLoader.efi`, and a
plain ARM64 XNU Mach-O image. It does not require an IPSW or a separate ESP.

## Boot chain and storage format

```text
Samsung S-Boot
  -> BootShim: relocate exactly 4 MiB of firmware to 0xC9000000
  -> Mu UEFI: initialize existing board, display and UFS services
  -> XnuLoader embedded in the firmware volume
  -> read GPT partition named `BOOT` or `boot` through EFI DiskIo/BlockIo
  -> parse XNU, build Apple DeviceTree and boot_args, ExitBootServices
  -> enter XNU's first CPU entry
```

The Android kernel field retains the reference `BootShim + firmware FD + DTB`
layout. Both the Android ramdisk and second-stage fields are empty. XNU is an
opaque trailer after the page-aligned Android kernel field: S-Boot does not need
to load or understand it. The EFI loader reads that trailer from storage.
This avoids introducing an unverified Samsung ramdisk physical load address.

The trailer begins at `page_size + align(kernel_size, page_size)`. Page size is
2048 bytes. Its 128-byte little-endian header is:

| Offset | Size | Value |
| --- | --- | --- |
| 0 | 8 | ASCII `B2LXNU01` |
| 8 | 4 | Version, 1 |
| 12 | 4 | Header size, 128 |
| 16 | 8 | Mach-O byte length |
| 24 | 8 | Mach-O offset from header, 128 |
| 32 | 64 | Host-generated random seed |
| 96 | 32 | Reserved, zero |
| 128 | variable | Thin ARM64 Mach-O |

The host seed changes when the image is packaged, **not on every reboot**.
The loader prefers the firmware RNG. The static seed fallback is only for
initial bring-up and is not sufficient for a production system's fresh boot
entropy requirement.

## Build the firmware profile

Use a disposable external Mu-Silicium build tree. The reference checkout must
remain untouched. Build the standalone loader first, then run:

```sh
python3 scripts/prepare-boot-firmware.py \
  --mu "$MU_WORK_TREE" \
  --loader "$LOADER_EFI" \
  --loader-guid 92742D9E-1A4F-42F8-9DC9-53895297B2F4
```

This adds a binary EFI application to the external firmware volume and removes
ACPI from this profile. It validates the actual `PlatformBootManagerLib` binding
and patches its post-console hook to connect controllers and start that exact
firmware application directly. This hook executes before BDS consumes cached
`BootNext` or an existing `BootOrder`. If the loader returns, the firmware stops
with an error rather than falling through to an old Windows boot entry.
The launch path does not create, delete or reorder persistent boot options.
The profile also bounds the GICv2 ExitBootServices interrupt drain and separates
the 10-bit interrupt ID from the complete SGI acknowledge token. It selects
combined non-secure EOI mode before draining, matching the XNU board setup.
This avoids accessing `GICC_DIR`, which is outside this board's existing 4 KiB
CPU-interface mapping; the MMIO map and reserved-memory guards stay unchanged.
This removes a shutdown hang risk; it is not a claim that a measured stall was
caused by that callback.
Rebuild that external firmware with the existing 4 MiB FD parameters. The XNU
repository contains no copied ACPI or Windows implementation.

Do not enlarge the FD: `0xC9000000..0xC9400000` is followed immediately by the
firmware stack. The packager rejects an FD that is not exactly 4 MiB.

## Package

```sh
python3 scripts/package-boot.py \
  --mu "$MU_WORK_TREE" \
  --firmware "$XNU_PROFILE_FD" \
  --kernel "$B2L_XNU_MACHO" \
  --loader "$LOADER_EFI" \
  --output-dir out/first-boot \
  --max-image-bytes 67108864

python3 scripts/package-boot.py --verify out/first-boot/boot.img
```

The ceiling in this example is an offline packaging constraint; it is **not a
claim that the connected phone's BOOT partition has that capacity**. Pass
`--boot-partition-bytes` only after reading the real beyond2lte partition size.

Outputs are `boot.img`, `manifest.json`, and an ordinary distribution archive
`beyond2lte-xnu-boot.tar`. The archive is not an Odin AP package. Intermediate
components are retained under `components/` for inspection.

The script checks the Android header, trailer bounds, physical FD layout, and
plain ARM64 Mach-O CPU type. It also decompresses the real FD's guided LZMA
firmware volume and requires the supplied loader binary to be embedded there;
an old firmware-only FD is rejected. The manifest distinguishes an offline build from
a device-verified boot. Firmware/UFS availability and S-Boot acceptance remain
runtime checks until this image has been tested on the actual beyond2lte.

## Device installation boundary

Packaging performs no ADB operations and writes no phone partitions. Before an
installation, verify that the device is **beyond2lte / SM-G975F**, that recovery
is active, and that the resolved `BOOT` partition can hold the entire image.
The user must approve the BOOT write after the completed artifact is presented.
No SYSTEM, USERDATA, recovery, or partition-table update is part of this package.

The first boot target is kernel entry, console output and timer/interrupt
progress. It is not a bootable iOS userspace or a SpringBoard image.
