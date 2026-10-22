# Build0042: firmware-owned low secondary entry

## Hardware evidence and hypothesis

The0041 video frames show CPU0 completing registration/initCPU, then CPU1
passing SMR, PRNG, scheduler idle-thread and IPC initialization. The final
visible line reaches CPU_ON for target1 with physical entry0x900506000 and
context0. No CPU_ON return or secondary-stage proof is visible before reset.
Thus registration ordering alone did not solve the reset. The captured boundary
is the firmware CPU_ON/high secondary-entry transition; it does not distinguish
EL3 rejecting the address, a failure after entry or another firmware event.

The CPU_ON wrapper preserves the64-bit entry in actual disassembly. The legacy
Samsung firmware accepting the64-bit function ID does not by itself establish
that it supports this particular high address. This experiment supplies a low
entry instead; it must not be described as a confirmed firmware truncation bug.

## Ownership and handoff

EFI selects a16KiB-aligned page range below4GiB from the current real
EfiConventionalMemory descriptors, then allocates it as EfiLoaderCode. Firmware
reservations are not relabeled or widened. This may use allocator-owned free
pages in the existing DXE heap; the heap boundary and live firmware objects are
preserved. The page is a firmware allocation retained after ExitBootServices,
not normal XNU RAM. The primary high arena and all CPU descriptors remain as0041.

The selector rejects malformed maps, overlapping aliases, runtime pages, holes,
wrong ownership, overflow, out-of-bank addresses, Samsung U-H, FD, stack,
framebuffer and auxiliary EL2/RT-log ranges. Fresh allocated-map ownership and
identity mapping are checked. Final ownership is checked again on each actual
ExitBootServices attempt. Allocation failure stops in EFI; it never falls back
to a guessed protected address or silently uses the previous high entry.

The loader copies a64-byte authored ARM64 template into the allocated page,
checks its instruction/magic/ABI words, and cleans/invalidates by actual cache
line sizes using CTR_EL0. No global cache set/way operations or EL2 writes are
introduced. The chosen DT carries the page's physical address and16KiB size.
Only the existing high arena is advertised as general memory.

A normal kernel coordinator maps this separately owned range before CPU
registration, validates its address, ABI, opcode words and pristine target/stage.
It fills a64-bit literal with the actual high secondary address, clears the
low-stage word, barriers and reads the target back. CPU_ON receives the low
physical page, with context0. The stackless template masks exceptions, records
entry in its page, barriers, loads the full64-bit target and BRs there. It does
not rely on firmware preserving a high context value, and never truncates the
high target. Existing XNU reset/mailbox/timer/IPI checks remain mandatory.

Only the entry template is prepared in EFI; kernel pages and CPU reset inputs
are still published by XNU when their actual addresses/state exist. This does
not replace the original high kernel or alter USERDATA/cache files in recovery.

## Offline verification

The real assembly is cross-assembled for both ELF and UEFI COFF; extracted
.text bytes are identical and exactly64bytes. Actual ADR/LDR offsets address
the low-stage and64-bit target literals, and the final branch is BR x16.
ASan/UBSan tests cover real template words, high target roundtrip, every bit
mutation in immutable words, protected addresses, EFI ownership, overlapping
aliases, holes, malformed descriptor stride and arithmetic overflow. Actual DT
tests cover the optional range's bounds, ABI and protected-address rejection.
Existing six-provider ordering, GIC, arena and package gates remain applicable.

Hardware low-entry acceptance, CPU1 C/MMU entry, six-core proof and sustained
runtime remain unverified until the next device test. Cache parallelism remains
blocked on that hardware proof; its current single worker is unchanged.

## Build and installation

Kernel and EFI builds complete. Actual scalar cache/emulator codegen, matching
stripped/unstripped/dSYM identities and changed-object timestamps pass. The
actual built EFI PE contains the verified64-byte entry template, and the FD
contains that exact loader. Only BOOT was updated after verifying physical0041
against its retained package. Full0042 image-prefix SHA256 readback matches;
reboot was requested. The known single-core0039 package remains available.
No recovery mount, raw USERDATA change, cache reset or reservation-map edit
occurred. All low-page address selection/ownership checks execute on-device in
EFI; host tests alone do not establish that firmware accepts the low entry.
