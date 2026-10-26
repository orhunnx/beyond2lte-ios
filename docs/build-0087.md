# 0087: EFI-owned discontiguous RAM

## Recovery evidence and scope

Read-only recovery inspection on the SM-G975F 8GiB target reports MemTotal
7,604,384 KiB, while the 0086 XNU log advertises a 4,024MiB arena. The runtime
DT contains six memory nodes, 25 fixed reserved-memory ranges and the final
40MiB EL2 early-memory reservation. A memory-only copy of these properties is
included under common/memory; the full private DT remains ignored in out/.

Subtracting those reservations, actual low-bank gaps, UEFI FD/stack, framebuffer
and the B2L EL2/runtime-log area leaves a whitelist totaling 7,044.890625MiB.
This is a ceiling, not an allocated amount. Live secure-world carveouts, active
EFI allocations, 16KiB VM-page alignment, the separate secondary trampoline and
the existing 32MiB EFI execution headroom can reduce it. Linux-created CMA pools
are not assumed to exist in the independent EFI/XNU boot.

## Firmware and loader

Only the disposable XNU firmware tree receives the expanded profile. The
reference UEFI/Windows memory policy remains unchanged. Broad budget guards
are replaced with all exact fixed DT reservations; absent low-bank DDR gaps
are explicitly reserved. Live secure-world hole queries must complete before
the RAM manager publishes ranges. Unknown enabled-feature errors remain closed,
including when a library constructor's return value is ignored. The live-hole
subtractor sorts, clips and merges holes without unsigned underflow.

The primary kernel allocation remains 4,024MiB with the existing bootstrap
mapping. The EFI loader allocates every eligible extra EfiConventionalMemory
span with AllocateAddress/EfiLoaderData and hands a sorted owned-range list to
XNU. Runtime/boot-service allocations, reservations and MMIO never become free
pages. Each final map-key/ExitBootServices attempt validates unique LoaderData
coverage for every advertised span and the independent trampoline. No physical
memory is tested by overwriting it from recovery.

## Native VM and pmap

Patch 0055 enables B2L's use of XNU's existing non-linear vm_page representation.
A dense physical index and its inverse index only owned pages; physical gaps
are excluded from pmap validity, metadata, object offsets and cache maintenance.
The native page iterator visits the actual sorted spans and excludes the primary
bootstrap prefix. The physical aperture maps each span separately and retains
normal kernel mapping protections. Total physical memory accounting includes
all owned spans, while boot_args.memSize continues describing the primary arena.
Physical I-cache invalidation uses phystokv_range, including split mappings,
instead of assuming PA - gPhysBase + gVirtBase works for low banks.

The changes are conditional on B2L. Existing eight-CPU/three-cluster scheduling,
input behavior, cache format and storage layout are retained.

## Checks and installation

ASan/UBSan execute the actual EFI selector/ownership validator, native page
iterator, physical aperture translation and I-cache wrapper. Tests cover every
candidate page's dense index roundtrip, aliases, holes, runtime attributes,
sub-page fragments, reserved GOP/EL2/secure-camera ranges, unordered/overlapping
live holes, overflow and ignored-constructor failures. All 25 fixed reservations
are independently checked against the prepared firmware table. DT/loader tests,
primary bootstrap geometry, real XNU and EFI builds pass. The full 55-patch
sequence verifies; the final linked kernel RCpc audit is zero.

0087 was written only to BOOT after recovery/product/capacity checks, a full
57,671,680-byte backup and exact 0086 prefix verification. The 23,518,696-byte
image and exact readback have SHA256:

`de9541f957ce3d8f3d412fd0c25d940fa572eda07ed5128b6888233d20edbdac`

The reboot command completed. Receipts, symbols, personalized images and backup
remain private ignored artifacts under out/0087. Actual allocated MiB, successful
VM startup and any change in UI latency await device evidence. The early log
prints B2L RAM: owned=... MiB ranges=... primary=... and each owned PA span.

## Device result

The target reported `8/8 CPUs initialized`, then panicked with
`B2L UFS: requested HFS root unavailable: 6`. Recovery could read the SYSTEM
log region, but its epoch 48 and bytes still matched 0086: 0087 did not start
the persistent UFS logger. This does not establish filesystem corruption.
The UFS transport still restricted DMA to gPhysBase/gPhysSize, although the
allocator could now return other owned banks. Build 0088 fixes that mismatch.
