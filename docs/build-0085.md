# 0085: three-tier native Edge scheduling

## CPU topology

0084 presented all eight CPUs as one symmetric cluster. That did not force
identical frequencies, but it gave the scheduler no performance-class distinction.
0085 enables ARM AMP and native Clutch/Edge with three classes:

| Logical CPUs | Hardware | DT class | Cluster / pset | CPU mask |
| --- | --- | --- | --- | --- |
| 0-3 | Cortex-A55 | E | 0 | 0x0f |
| 4-5 | Cortex-A75 | M | 1 | 0x30 |
| 6-7 | Samsung M4 | P | 2 | 0xc0 |

XNU's M class means the middle performance class; it does not mean Samsung M4.
Firmware CPU_ON IDs and the measured MIDR/MPIDR identity handling are retained.
A55 and A75 share an affinity namespace, so the B2L topology parser uses the
board's explicit cluster-id property instead of deriving groups from AFF1.
The CPU provider validates the three masks, each CPU's type and local core ID.
No CPU is disabled, and no numeric capacity ratio or measured frequency is invented.

## Placement policy

The board has no Apple CLPC provider to update Edge recommendations. Therefore
new clutch groups receive these initial native pset preferences:

| Work class | Preferred hardware |
| --- | --- |
| Foreground, user initiated, user default and user fixed-priority buckets | M4 |
| Utility | A75 |
| System group, including kernel/cache workers and launchd, except background QoS | A75 |
| Background QoS | A55 |

These are recommendations, not hard processor bindings. Native priority rules,
priority inheritance, preemption, load checks and Edge migration remain in use.
The existing three-tier Edge defaults allow P to spill to M before E, and M
to E. Background E work does not spill upward by default. There is no DVFS or
voltage change in this build; separate frequency measurements are still needed.

## ARMv8 atomic compatibility

The newly enabled Edge stir-the-pot registry expects CAS-style transactions and
normally uses a128-bit registry. B2L has only eight CPUs, requiring16 state bits,
so its registry uses64 bits with the original size assertion retained. Candidate
computation happens outside the exclusive transaction; a checked compare-exchange
retries against any changed value. This prevents other reads/function calls in
candidate scanning from invalidating an LL/SC reservation indefinitely. Only
B2L uses this fallback; the existing non-B2L implementation remains intact.
No LSE, CASP, RCpc or Apple CPU target is enabled to satisfy the build.

## Diagnostics and checks

Boot records print every Edge pset's cluster, type and mask plus the initial
QoS preferences. Existing progress snapshots append the CPU number observed
at that syscall/message boundary. These are placement samples, not utilization
percentages; hard-bound startup work and inherited priorities can differ from
a thread's initial preferred pset.

The host test feeds the actual EFI tree through the production XNU topology
parser and checks all eight firmware IDs, three masks, local core IDs, QoS
preferences, spill order and restricted-CPU fallback. The actual registry
macros/state methods pass every CPU/state combination, abort/override behavior,
a forced compare-exchange failure preserving a neighboring update, and eight
concurrent writers. ASan/UBSan stop on errors. Existing DT/loader, generic CPU,
SMP registration ordering and progress snapshot checks pass. Production XNU,
EFI and embedded firmware builds pass. The linked kernel contains zero RCpc
instructions; the scheduler object disassembly contains no LSE/CASP instructions.

## Installation and remaining runtime check

0085 was written only to BOOT after recovery/product/capacity checks, a full
57,671,680-byte backup and verification of the installed0084 prefix. The
23,532,696-byte image and exact readback both have SHA256:

`11b57a135e7f483d5af538ea1bd598c517eef8033014e9b80c5f9ae5a4c26989`

The device was rebooted. Receipt and rollback image are private ignored files
under out/0085. WiFi metadata remains private. Three-cluster startup, actual
foreground placement and any improvement in logo/home-screen latency require
the next physical-device run. This build does not claim to solve the entire
50-minute delay or the incomplete late native-log coverage observed in0084.

## Subsequent device evidence and correction

The recovered 0085 log verifies all eight CPUs, the three Edge psets and
per-CPU timer/bidirectional SGI1 checks. It contains no kernel panic, but no
first copied IOSurface frame appears during about 3,103 seconds. Fresh progress
samples for SpringBoard and backboardd appear only on CPUs 0-5. These samples
are not utilization measurements or proof of hard binding.

The system-group override above was a placement mistake: THREAD_GROUP_SYSTEM
is shared with launchd and userspace services, so it cannot identify kernel-only
work. [0086](build-0086.md) removes that override and assigns only the explicit
IO-storage group to the middle cluster. This is a concrete regression found in
0085; it does not establish the cause of every earlier startup delay.
