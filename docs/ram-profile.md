# XNU RAM profile for the verified 8GiB beyond2lte

This profile applies only to the disposable external XNU firmware build. The
working UEFI reference and its Windows memory policy remain separate.

The live recovery device tree contains an additional final2GiB bank absent
from the bundled stock DTS. The live fixed reservations also include
`el2_earlymem` at `0x9FD800000..0xA00000000` (40MiB). This real reservation is
preserved. The earlier last-bank2GiB guard was a conservative Windows budget;
replacing it with the exact40MiB reservation exposes only the remaining DDR.
No low-bank, first-upper-bank, firmware, framebuffer or MMIO entry is changed.
The runtime DT capture has25 fixed reserved-memory `reg` ranges; all25 remain
covered by reserved descriptors in the resulting firmware table.

| Range | Policy |
| --- | --- |
| `0x80000000..0x100000000` | Original low-bank guards, FD4MiB, stack, heap, GOP, EL2 and log unchanged |
| `0x880000000..0x900000000` | Entire first upper bank remains reserved, including `sec_rdx_bootdev` |
| `0x900000000..0x9FD800000` | Up to4056MiB conventional memory, discovered from actual DDR by RamManagerDxe |
| `0x9FD800000..0xA00000000` | Actual40MiB EL2 early memory, reserved |

The loader selects only a single conventional descriptor inside the third
range, reserves32MiB headroom, and allocates the chosen span using
`AllocateAddress/EfiLoaderData`. Thus its maximum advertised XNU RAM is
**4024MiB**, not4GiB. Firmware allocations or fragmentation can reduce this
number. EFI status and the final memory map determine the actual result.
The loader validates complete, unique LoaderData ownership after allocation
and again before each ExitBootServices attempt. Gaps, aliases, changed memory
types, runtime descriptors and bounds violations stop the handoff.

XNU already uses40physicaladdressbits in TCR and can represent these addresses;
35bits cannot address36GiB. The B2L bootstrap table budget is increased from10
to16pages: two L1 roots, at mostfive L2 tables per RAM mapping and one GOP L2
require at most13pages for this arena. The extra pages hold tables, not general
RAM outside the allocation.

The proof is source and host validation, not a device boot result. The full
kernel/EFI build and installation checks pass in 0031; the next device screen
and recovered early log must establish the actual advertised RAM size.
