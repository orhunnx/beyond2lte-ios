# Exynos 9820 first-boot runtime

The B2L target is plain ARM64, ARMv8-A with crypto extensions, 4 KiB pages,
without Apple pointer authentication, BTI, Apple MMU monitors or secondary CPUs.
It runs only the CPU on which firmware launches the EFI application. It does
not assume that this CPU has MPIDR 0; the loader records the actual MPIDR.

## Display and progress evidence

The loader supplies the existing UEFI GOP framebuffer. XNU does not reprogram
the panel, display controller, GPU, clocks or display power domains.

1. `_start_first_cpu` writes a 16 by 16 green square at pixel (128, 128), using
   the physical framebuffer before enabling the MMU. This establishes entry
   into XNU assembly, not completion of kernel initialization.
2. Immediately after `arm_vm_init`, XNU maps the physical framebuffer, acquires
   its built-in text console and initializes printing before KPRINTF startup.
   The initial square is cleared when the console acquires the screen.
3. `B2L GIC: GICv2 IRQ ready` establishes register programming only.
4. `B2L CPU: 1/1 CPU initialized` establishes CPU registration with the scheduler.
5. `B2L IRQ proof: timer=... SGI1=... scheduler awake` is printed after a timed
   kernel-thread sleep. Both counters must be nonzero, so this is runtime
   evidence of actual timer and self-SGI dispatch. It is emitted three times.

These are milestones to observe on the physical device. Compilation and
symbol inspection alone do not establish that any of them occurred.

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
