# Build 0040: six-CPU A55/A75 startup diagnostics

## Request and device evidence

Expose the four Cortex-A55 and two Cortex-A75 cores. Leave both Samsung M4
cores absent. Recovery was read only: possible/present/online all report0-7;
MIDRs identify CPUs0-3 as A55,4-5 as A75 and6-7 as Samsung cores. Its live
CPU reg properties are0,1,2,3,4,5,0x100,0x101. All use enable-method=psci;
the live PSCI node uses compatible=arm,psci, method=smc, CPU_ON0xc4000003,
CPU_OFF0x84000002 and CPU_SUSPEND0xc4000001.

The new CPU description contains only MPIDR0..5. One scheduler cluster is a
bring-up simplification for these Aff1=0 CPUs; it does not claim identical
microarchitecture, DVFS, energy policy or measured per-core performance.
M4 MPIDRs0x100/0x101 are never submitted to CPU_ON. Memory allocation and
all existing firmware reservations are unchanged.

## LineageOS comparison

Pinned public revision: `a412cfaf86b980ce35cae96a35f678c8ea551575`
(branch lineage-23.2 when fetched). Original files are inspected outside this
repository; Linux implementation code is not copied into the board overlay.

- [Exynos DT CPU and PSCI nodes](https://github.com/LineageOS/android_kernel_samsung_exynos9820/blob/a412cfaf86b980ce35cae96a35f678c8ea551575/arch/arm64/boot/dts/exynos/exynos9820.dts#L326)
  match the live recovery DT, including legacy arm,psci and the SMC conduit.
- [ARM64 PSCI CPU boot](https://github.com/LineageOS/android_kernel_samsung_exynos9820/blob/a412cfaf86b980ce35cae96a35f678c8ea551575/arch/arm64/kernel/psci.c#L48)
  supplies the actual logical-to-physical MPIDR and physical secondary_entry.
- [Firmware PSCI](https://github.com/LineageOS/android_kernel_samsung_exynos9820/blob/a412cfaf86b980ce35cae96a35f678c8ea551575/drivers/firmware/psci.c#L193)
  passes a zero context; legacy IDs come from DT, without requiring successful
  PSCI_VERSION discovery. The SMC wrapper zeroes unused argument registers.
- [Secondary startup](https://github.com/LineageOS/android_kernel_samsung_exynos9820/blob/a412cfaf86b980ce35cae96a35f678c8ea551575/arch/arm64/kernel/head.S#L611)
  performs CPU/MMU setup before secondary C startup. Linux's el2_setup supports
  entry at more than one EL. This XNU experiment accepts EL1 only; unexpected
  EL parks and does not reconfigure Samsung EL2.
- [GIC CPU mask discovery](https://github.com/LineageOS/android_kernel_samsung_exynos9820/blob/a412cfaf86b980ce35cae96a35f678c8ea551575/drivers/irqchip/irq-gic.c#L463)
  scans banked ITARGETSR words instead of assuming the first byte is nonzero.
  The board now follows that discovery algorithm and validates one-hot unique
  masks. Remote SGI writes retain the required preceding memory barrier.

## Change and hardware gate

Patch0028 reintroduces SMP after the0033 rollback without removing newer
persona, DER entitlement, cache or crash changes. MAX_CPUS6 requires a fresh
separate build, including regenerated exported headers and assembly offsets.
The EFI ADT carries exactly six CPU nodes and cpus=6.

The provider records registration and CPU_ON begin/result to the early screen.
Each secondary has its own128-byte RAM mailbox; stackless ARM64 assembly
publishes stages1(entry),2(EL1 accepted),3(reset lookup),4(CPU data found),
5(start_cpu),255(unexpected EL). CPU0 reads dedicated cache lines; no firmware
reserved-memory scratch address or early disk write is introduced. Stage5
alone does not prove that MMU initialization completed.

After all cores reach initCPU and self-SGI readiness, a normal observer thread
checks a real timed wait/timer interrupt on every CPU and CPU0-to-each-secondary
and each-secondary-to-CPU0 XNU xcalls. Only then is gIOAllCPUInitialized
published and the6/6 proof printed. Errors and waits are bounded.

The0032 reset remains unexplained: it produced no new persistent epoch or
CPU_ON result. The broader GIC target scan is a source-backed improvement,
not a demonstrated diagnosis of that reset. A high-address secondary entry is
kept64-bit; EL3 acceptance still requires hardware evidence.

## Cache findings from0039 recovery log

The capture is complete for its saved118444 bytes: epoch12,84valid records,
no missing positions or producer drops. Its tail may precede later runtime.
It confirms one CPU, no captured early panic, but repeated lsd aborts persist;
0039 must not be described as having fixed lsd. The sample ends at queued33969,
learned1186, applied1016, skipped12140, dropped239288, errors0.

`queued` is cumulative accepted submissions, not outstanding requests. The
128-entry ring has at most127 outstanding requests. The single worker drains
at most128 entries then sleeps one second. Direct-mapped hints can be replaced
by collisions or new process mappings. learned counts persisted instruction
updates, not newly occupied pages. Existing-known, excluded and full-cache
work does not increase learned. The drop counter mixes queue contention,
queue-full and no-free-slot events; it cannot establish disk throughput alone.

Pre-init applied1016 + already4445 accounts for all5461 cache slots. More CPUs
cannot create free slots, and the current UFS transport is serialized behind a
mutex. Cache parallelism is explicitly deferred until sustained SMP proof.
The later design should parallelize resolution/validation with per-worker
buffers and serialize per-page version merging plus A/B durable commit. It
must retain undo pages, corecrypto exclusions, readback and flush ordering.

## Verification status

Offline: actual ADT encoding and all short capacities, exact CPU0..5 identities,
M4 boot-MPIDR rejection, retained security properties, existing arena guards,
GIC mask/SGI encoding and patch upgrade/idempotence/local-edit refusal pass.
Build, installation and sustained hardware proof are separate gates below.

## Completed local build

The clean MAX_CPUS6 kernel links successfully. Its Mach-O inspection passes;
stripped/unstripped/dSYM identities match. The actual SMC disassembly passes
0xc4000003, full-width physical entry, requested MPIDR, zero context and zero
x4..x7. Native cache and emulator objects retain scalar-only compiler flags
and actual machine code. EFI and the embedded-loader verification complete.
The BOOT package fits the confirmed partition capacity. These checks do not
establish that any secondary core ran successfully on hardware.

## Installation

Only BOOT was updated after verifying the physical previous0039 BOOT against
its retained package. Complete0040 image-prefix SHA256 readback matches the
local package. Reboot was requested. No recovery filesystem mount, journal
replay, raw USERDATA write, cache clearing or partition-layout change occurred.
The cache worker is unchanged. Hardware6/6 proof and sustained runtime remain
pending the device screen or a subsequent saved log; no speedup is claimed.
