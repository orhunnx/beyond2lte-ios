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
