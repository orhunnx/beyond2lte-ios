# EFI to XNU handoff

`XnuLoader.efi` is a standalone AArch64 EFI application. Its firmware file GUID
is `92742D9E-1A4F-42F8-9DC9-53895297B2F4`. It is built separately from the
reference firmware's ACPI and Windows platform material.

## Input and storage boundary

The preferred source is the logical GPT partition named exactly `BOOT`, read
through EFI Block I/O and Disk I/O. An Android v1 header locates a bounded opaque
`B2LXNU01` trailer after the page-aligned kernel field. The Android ramdisk size
stays zero, so this data is not offered to Samsung S-Boot as a Linux initramfs.
The trailer contains a 128-byte header followed by a plain ARM64 Mach-O kernel.
The payload reader rejects recognized but malformed payloads. It never writes
partitions or persistent EFI variables.

For independent EFI Shell testing, the fallback source is
`\EFI\B2L\kernel` on the application's own filesystem volume. The file must be
fully linked `MH_EXECUTE` for ARM64, with a file-backed executable entry, bounded
non-overlapping segments, and no chained pointer fixups or dylib dependencies.
AArch64 PAC (`arm64e`) kernels are rejected. The virtual slide is zero.

## Entropy

EFI RNG is preferred. If unavailable, the loader uses the 64-byte host-generated
seed in the BOOT payload, or `\EFI\B2L\seed.bin` for filesystem loading.
The packager must generate this seed from the host operating system's random
source. The loader does not substitute timestamps, counter values, zeros or
fixed constants for entropy.

A packaged seed is new for each package, **not for each reboot of that package**.
Repeated booting without firmware RNG reuses that initial entropy. This fallback
is suitable for the current development bring-up, and is not a production boot
entropy design. Do not publish a device package's seed as a reusable template.

## RAM and ABI

EFI allocates a contiguous 514 MiB `EfiLoaderData` range. Inside it, a 2 MiB
aligned 512 MiB arena contains the kernel segments, Apple flattened device tree
and `boot_args`. Only this allocation is advertised as available RAM to XNU.
Reserved firmware, framebuffer, MMIO and unrelated RAM are excluded. This first
boot configuration deliberately uses one CPU and a limited memory arena.

The loader follows `rel/xnu-12377`'s ARM64 ABI: a 1,152-byte `boot_args`, a
1,024-byte command line at offset 108, physical boot arguments in `x0`, and the
device-tree pointer translated into the kernel's linked virtual address space.
The Apple device tree is native little endian and includes the actual boot
CPU's MPIDR affinity, actual `CNTFRQ_EL0`, a single running CPU, Exynos 9820 SoC
range, GICv2 distributor/CPU-interface ranges, and the GOP framebuffer.
Unmeasured CPU and bus clocks are not advertised as counter frequencies.

GOP must expose direct 32-bit B,G,R,X pixels and at least 144 by 144 pixels. The
physical framebuffer address is marked with the XNU `v_baseAddr` physical flag;
text mode is selected for kernel bring-up. The framebuffer is a root-level
Apple DT node with an absolute physical register range.

## Exit and machine state

The loader requires EL1 and validates identity mappings for its image, current
stack and the loaded kernel data. It disables the EFI watchdog, obtains the
final memory map, and retries `ExitBootServices` on stale keys. After the first
exit attempt it invokes only `GetMemoryMap` and `ExitBootServices`; failures halt
without calling console, allocation or cleanup boot services.

The stackless assembly trampoline masks exceptions, cleans and invalidates data
caches by architected set/way geometry, invalidates instruction caches, clears
EL1 MMU/data/instruction-cache enables, invalidates TLBs and branches to the
kernel's physical entry with `x0 = boot_args physical address`. Both legacy and
FEAT_CCIDX cache-geometry formats are supported. It does not use the firmware
library's whole-cache function, which asserts rather than performing that
operation in the pinned firmware revision.

## Verification status

Host sanitizer tests cover Mach-O bounds/entry validation, Apple DT decoding and
all insufficient buffer capacities, topology/seed contents, and `boot_args`
layout. The EFI application has been compiled and inspected as an ARM64 EFI
application. These checks do not establish successful device execution. Actual
entry, GIC/timer behavior and subsequent kernel progress require the device
screen or runtime logs.
