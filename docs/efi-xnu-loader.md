# EFI to XNU handoff

`XnuLoader.efi` is a standalone AArch64 EFI application. Its firmware file GUID
is `92742D9E-1A4F-42F8-9DC9-53895297B2F4`. It is built separately from the
reference firmware's ACPI and Windows platform material.

## Input and storage boundary

The preferred source is the logical GPT partition named exactly `BOOT`, read
through EFI Block I/O and Disk I/O. An Android v1 header locates a bounded opaque
`B2LXNU02` trailer after the page-aligned kernel field. The Android ramdisk size
stays zero, so this data is not offered to Samsung S-Boot as a Linux initramfs.
The trailer contains a 320-byte header followed by a plain ARM64 Mach-O kernel.
The payload reader rejects recognized but malformed payloads. It never writes
partitions or persistent EFI variables.

For independent EFI Shell testing, the fallback source is
`\EFI\B2L\kernel` on the application's own filesystem volume. The file must be
fully linked `MH_EXECUTE` for ARM64, with a file-backed executable entry, bounded
non-overlapping segments, and no chained pointer fixups or dylib dependencies.
AArch64 PAC (`arm64e`) kernels are rejected. The virtual slide is zero.

## Entropy

EFI RNG is preferred. If unavailable, the loader uses the 256-byte host-generated
seed in the BOOT payload, or `\EFI\B2L\seed.bin` for filesystem loading.
The packager must generate this seed from the host operating system's random
source. The loader does not substitute timestamps, counter values, zeros or
fixed constants for entropy.

A packaged seed is new for each package, **not for each reboot of that package**.
Repeated booting without firmware RNG reuses that initial entropy. This fallback
is suitable for the current development bring-up, and is not a production boot
entropy design. Do not publish a device package's seed as a reusable template.

## RAM and ABI

EFI selects the largest 2 MiB aligned range within one current
`EfiConventionalMemory` descriptor and allocates it exactly as `EfiLoaderData`.
The arena is at least 1 GiB, capped at 2 GiB for this bring-up, and leaves
32 MiB of that descriptor available for firmware headroom. The actual size in
MiB is printed before handoff and supplied to XNU. If no contiguous 1 GiB range
is available, loading stops with an explicit error. Kernel segments, Apple
flattened device tree and `boot_args` occupy the start of that arena. Only this
allocation is advertised as available RAM to XNU.
Reserved firmware, framebuffer, MMIO and unrelated RAM are excluded. This first
boot configuration deliberately uses one CPU and a limited memory arena.

The loader follows `rel/xnu-12377`'s ARM64 ABI: a 1,152-byte `boot_args`, a
1,024-byte command line at offset 108, physical boot arguments in `x0`, and the
device-tree pointer translated into the kernel's linked virtual address space.
The Apple device tree is native little endian and includes the actual boot
CPU's MPIDR affinity, actual `CNTFRQ_EL0`, a single running CPU, Exynos 9820 SoC
range, GICv2 distributor/CPU-interface ranges, and the GOP framebuffer.
Unmeasured CPU and bus clocks are not advertised as counter frequencies.
The tree also includes the mandatory `/chosen/memory-map` and `/defaults`
containers. They have no fabricated trust-cache, auxiliary-kernel, protected I/O
range or filter properties; absent optional values retain the kernel defaults.
`/chosen/kernel-ctrr-to-be-enabled` is an explicit native 32-bit zero because
Apple CTRR is unavailable on this platform. XNU consumes this policy during
TUNABLES startup and retains its initialized-state assertion.

GOP must expose direct 32-bit B,G,R,X pixels and at least 1,184 by 864 pixels
for the enlarged handoff progress panel. The
physical framebuffer address is marked with the XNU `v_baseAddr` physical flag;
text mode is selected for kernel bring-up. The framebuffer is a root-level
Apple DT node with an absolute physical register range.

## Exit and machine state

The loader requires EL1 and validates identity mappings for its image, current
stack and the loaded kernel data. It disables the EFI watchdog, obtains the
final memory map, and retries `ExitBootServices` on stale keys. After the first
exit attempt it invokes only `GetMemoryMap` and `ExitBootServices`; failures halt
without calling console, allocation or cleanup boot services.

The stackless assembly trampoline masks exceptions and cleans the populated
kernel/DT/boot arguments, loader image and current stack ranges to PoC using
`DC CVAC`. It invalidates their instruction cache lines using `IC IVAU`, clears
EL1 MMU/data/instruction-cache enables, invalidates TLBs and branches to the
kernel's physical entry with `x0 = boot_args physical address`. Cache-line sizes
come from `CTR_EL0`; no global set/way operations, `CSSELR_EL1`, `CCSIDR_EL1` or
`ID_AA64MMFR2_EL1` accesses are needed. This removes operations that an EL2
monitor may trap without changing kernel data coherency. It does not establish
that an EL2 trap caused the first build's stall.

Six large, directly rendered rows remain independent of EFI console services:
`MAP READY`, `EBS RETURN`, `HANDOFF`, `CACHE CLEAN`, `MMU OFF`, `KERNEL JUMP`.
Gray means not reached, yellow/cyan means the relevant operation is in progress,
green means it returned/completed, and red denotes an EFI map/exit error. The
final four rows are updated by the stackless assembly itself. The kernel adds
its own separate entry marker before its first system-register access. These
make an EFI exit callback stall distinguishable from cache maintenance, MMU
transition or early kernel code. Drawing after the first exit attempt uses only
CPU stores and barriers, never EFI boot services.

## Verification status

Host sanitizer tests cover Mach-O bounds/entry validation, Apple DT decoding and
all insufficient buffer capacities, topology/seed contents, and `boot_args`
layout. Direct framebuffer panel bounds and stage updates also pass host
sanitizer tests. The EFI application has been compiled and inspected as an ARM64 EFI
application. These checks do not establish successful device execution. Actual
entry, GIC/timer behavior and subsequent kernel progress require the device
screen or runtime logs.
