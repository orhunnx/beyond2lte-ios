# UEFI to XNU boot contract

## Firmware boundary

The existing beyond2lte firmware runs from the Android BOOT partition. Samsung
S-Boot still starts that firmware; this project adds the subsequent XNU path.
The firmware is maintained in its own repository.

Use EFI memory descriptors to select RAM at runtime. The firmware's reserved
ranges include its image, stack and framebuffer. Those ranges must not be
reused as kernel RAM, even when the corresponding physical addresses refer
to DRAM. Reference constants are not a substitute for a live EFI memory map.

Firmware ACPI tables and Windows support are outside this repository's scope.
XNU receives its own Apple-format device tree.

## XNU 12377 entry ABI

`pexpert/pexpert/arm64/boot.h` defines the ARM64 `boot_args` structure. The
current ABI includes a 1024-byte command line and is 1152 bytes in total.
`CommandLine` starts at byte 108; the video structure starts at byte 40.
Older XNU ports using a 608-byte command line are not ABI-compatible.

`start_first_cpu` in `osfmk/arm64/start.s` accepts the physical address of this
structure in `x0`. It establishes the kernel translation tables and continues
to `arm_init`. It is not an EFI application entry point.

The loader must supply matching `virtBase`, `physBase`, `memSize`,
`topOfKernelData`, video fields and an Apple-format device tree. `deviceTreeP`
is expressed in the kernel's virtual address space. The physical kernel
entry must correspond to the loaded Mach-O entry point.

## Mandatory Apple device-tree containers

The loader supplies these nodes for the current non-SPTM ARM64 path:

| Path | Required contents | Consumer |
| --- | --- | --- |
| `/chosen` | `random-seed` (256 bytes), `dram-base` and `dram-size` (64-bit arena bounds), `kernel-ctrr-to-be-enabled` (native 32-bit zero) | PRNG, `arm_init` and kernel-text policy initialization |
| `/chosen/memory-map` | Container with a `name`; no range properties for absent payloads | `arm_vm_prot_init` |
| `/defaults` | Container with a `name`; omitted tuning properties preserve kernel defaults | pmap ASID/PV initialization |
| `/cpus/cpu0` | `state=running`, 32-bit MPIDR affinity `reg`, actual counter frequency | CPU topology and clock setup |
| `/arm-io` | SoC `device_type` and native 64-bit `ranges` | Platform expert |

In `rel/xnu-12377`, `arm_vm_prot_init` asserts that `/chosen/memory-map` exists
before optionally reading `TrustCache`, `AuxKC` and `AuxKC-mach_header`.
Those properties remain absent when the loader has no corresponding payload;
creating the container does not claim or authorize any protected memory range.

Likewise, pmap requires `/defaults` even when it uses its built-in ASID and PV
allocation policy. Optional `pmap-io-ranges` and `pmap-io-filters` remain absent.
No artificial I/O ownership or filter entries are added to satisfy lookups.

Exynos 9820 has no Apple CTRR provider. The loader explicitly publishes
`/chosen/kernel-ctrr-to-be-enabled` as a four-byte native-endian zero, rather
than claiming that CTRR will protect kernel text. XNU's TUNABLES startup handler
requires this property, even when its value is zero, before
`ml_unsafe_kernel_text()` may be queried. The kernel's initialization assertion
is retained; no assertion or text-protection state is bypassed.

## Platform configuration

B2L is a separate machine configuration. `VMAPPLE` assumes an Apple virtual
platform and is not a substitute for an Exynos 9820 port.

The initial target uses plain ARM64, ARMv8-A with crypto instructions, 4 KiB
pages, one CPU, GICv2 and the architected virtual timer. It does not use Apple
PAC, BTI, SPTM, PPL, AMCC or Apple production-fuse policy. Successful
compilation does not validate these runtime assumptions on the phone.

## Loader acceptance requirements

1. Check the running exception level, direct framebuffer format and available
   EFI conventional memory. Preserve firmware-reserved regions.
2. Validate every Mach-O command, segment range, arithmetic operation and
   executable entry point before copying data. Reject unsupported formats.
3. Allocate kernel memory through EFI, populate the Apple device tree and
   prepare the exact `boot_args` ABI.
4. Obtain a fresh memory-map key for `ExitBootServices`; handle a changed key
   without allocating memory after a successful exit.
5. Complete cache maintenance and satisfy the kernel's EL1/MMU entry contract.
   Do not call EFI services after exiting boot services.
6. Verify kernel entry and continued initialization using device output.

A firmware-only BOOT image must not be presented as a working XNU image.
