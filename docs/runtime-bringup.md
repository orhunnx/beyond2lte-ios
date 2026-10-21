# Exynos 9820 first-boot runtime

The B2L target is plain ARM64, ARMv8-A with crypto extensions, 4 KiB pages,
without Apple pointer authentication, BTI, Apple MMU monitors or secondary CPUs.
It runs only the CPU on which firmware launches the EFI application. It does
not assume that this CPU has MPIDR 0; the loader records the actual MPIDR.

## Display and progress evidence

Build 0002 reached XNU assembly on the device: the user confirmed its separate
green entry marker. C initialization and working interrupts remain unverified.

The kernel owns the text renderer. It follows the A5 port's early-console and
framebuffer serial-sink lifecycle, using the existing firmware GOP geometry
from `boot_args` rather than any A5 addresses. Its glyph bitmap is upstream
XNU's open-source `iso_font`, not a font extracted from an IPSW or KDK.

The display controller and its original reserved physical window remain intact.
No GPU, panel, clock or power-domain reprogramming is needed to draw CPU text.

| Visible milestone | What it establishes |
| --- | --- |
| Separate green rectangle near the bottom | First XNU instructions executed before system-register initialization |
| `XNU ENTRY` | Stackless assembly font rendering before MMU setup |
| `MMU PREP` | RAM and bounded GOP bootstrap mappings were installed |
| `MMU ON` | Execution continued after enabling the MMU |
| `ENTER C` | Assembly is about to branch to the C bootstrap |
| `B2L XNU C ENTRY` | `arm_init` entered with a usable early framebuffer |
| `B2L XNU 1..17: ...` | C bootstrap, VM transition and serial-sink milestones |
| `B2L EARLY PANIC: ...` | Panic formatting before ordinary KPRINTF initialization |
| `B2L GIC: GICv2 IRQ ready` | Controller register programming only |
| `B2L CPU: 1/1 CPU initialized` | Boot CPU registered with the scheduler |
| `B2L IRQ proof: timer=... SGI1=... scheduler awake` | Real interrupt delivery and timed scheduler wakeup; both counters must be nonzero |

Assembly text starts at y=336, and C text around y=480, so a cropped photograph
need not include the bottom marker. Text pixels are genuinely expanded 3x at
1440 pixels (24x48 cells), or 2x at widths 800 through 1199. The early renderer
wraps through text rows and clears the next row, avoiding a large scroll copy.

The output-only framebuffer sink registers before the stock serial-device
phandle checks, then carries both `printf` and `kprintf`. It does not invent
UART input, receive interrupts or a keyboard. No late stock video-console
acquisition clears the early stage history.

These milestones must be observed on the physical device. Compilation and
symbol inspection do not establish that C initialization or interrupts worked.

### Mapping lifetime and unchanged physical guards

The original firmware `DisplayReserved` region is `0xCA000000..0xCB400000`
(20 MiB). The 1440x3040, stride-5760 visible buffer ends at `0xCB0B3000`.
Its 2 MiB-aligned mapping ends at `0xCB200000`, inside that existing guard.
Both bootstrap assembly and the final mapper reject any rounded GOP extent
outside the original window. These pages are never advertised as normal RAM.

Two extra static bootstrap table pages reside inside the kernel image. They
map the already reserved GOP through TTBR0 without increasing the managed RAM
arena. Before TTBR0 is removed, XNU installs the same bounded device-memory
mapping at a reserved kernel VA below `LOW_GLOBAL_BASE_ADDRESS` and relocates
the console pointer. That virtual gap is already excluded from the allocator
by `pmap_virtual_region(3)`. The physical framebuffer remains the same.

## Interrupt controller and clock

The hardware addresses come from the existing Exynos 9820 device tree:

| Resource | Physical address / value |
| --- | --- |
| GICv2 distributor | `0x10101000`, length `0x1000` |
| GICv2 CPU interface | `0x10102000`, length `0x1000` |
| Architected virtual timer | PPI / INTID 27 |
| Kernel CPU signals | SGI 1 |
| Expected counter frequency | 26 MHz; read from `CNTFRQ_EL0` at runtime |

The implementation uses non-secure Group 1 IRQ delivery. The IRQ dispatcher
masks the GICv2 IAR with `0x3ff`, preserves SGI source bits for EOI, ignores
special acknowledgements 1020 through 1023, routes INTID 27 to `rtclock_intr`,
and routes SGI 1 to `cpu_signal_handler`. Combined EOI/deactivation is selected.

Before enabling these sources, stale non-secure firmware peripheral SPIs and
unused banked PPIs are masked. Secure-world ownership is preserved; accesses to
secure-only interrupt bits remain subject to the GIC's non-secure restrictions.
Unexpected enabled peripheral IRQs panic with the interrupt number because this
first-boot target has no peripheral interrupt drivers.

The timer uses `CNTV_*` registers, so no fake MMIO timer node is supplied.
`CNTFRQ_EL0` initializes XNU's timebase and decrementer frequency. CPU, bus and
memory clock frequencies remain unknown (zero); they are not invented from
the independent architected counter frequency.

## Apple device tree contract

All numerical values in this Apple device tree are native little-endian values,
not flattened Linux device-tree cells.

- Root compatible: `samsung,beyond2lte`.
- `/arm-io`: `device_type = "soc"`; `ranges` is three 64-bit values with
  the second equal to physical SoC base `0x10000000`.
- `/arm-io/gic`: `reg` is four 64-bit values:
  `[0x101000, 0x1000, 0x102000, 0x1000]`.
- `/cpus/cpu0`: `device_type = "cpu"`, `compatible = "b2l,boot-cpu"`,
  `state = "running"`, 32-bit `reg` containing the executing CPU's masked MPIDR.
  Cluster type is SMP. Only one CPU node is present.
- Boot video: 32-bit GOP pixels with valid stride/dimensions. The physical
  framebuffer address carries bit 0 set, the XNU physical-address indicator;
  the assembly marker masks the low two bits before access.

## Deliberate first-boot limits

The platform expert publishes the CPU without requiring Apple NVRAM. The CPU
driver follows XNU 12377's `ml_processor_register` behavior, which already calls
`processor_boot`; it does not start the boot CPU a second time.

PSCI secondary CPU startup, suspend/resume, storage drivers, USB, input,
GPU acceleration, iOS user space and writable root filesystems are outside this
first-boot runtime. Missing drivers are not represented by success-returning
stubs. An expected later inability to locate a root filesystem must be reported
as such rather than as a completed operating-system boot.

## Software text console checks

The B2L scroll path copies exactly the visible byte width per physical row,
using overlap-safe order. It avoids the generic open-source ARM scroll helpers,
which multiply an already byte-based pointer difference by four.

`python3 tests/test_console_raster.py /path/to/patched/xnu` extracts the actual
B2L glyph, cursor and scrolling functions, compiles a host harness with
AddressSanitizer/UndefinedBehaviorSanitizer, and checks 2x/3x raster expansion,
last-cell bounds, stride padding and both overlapping scroll directions.
This checks software geometry; it does not establish that a device booted.

`python3 tests/test_early_console.py /path/to/patched/xnu` runs the actual early
renderer against host-backed buffers with ASan/UBSan. It checks ring wrapping,
carriage return, high-bit character indexing, stride-padding canaries, pointer
relocation and rejection of geometry outside the original display guard.
Only the MMIO pointer and architectural barrier are substituted in the harness.
