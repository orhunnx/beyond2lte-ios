# Build0076: eight-CPU M4 trial

## Scope and recovery evidence

Recovery exposes CPUs0–7 online. Read-only identification registers show
CPUs0–3 MIDR0x411fd050(A55),4–5 0x412fd0a1(A75),6–7 0x531f0030(Samsung M4).
Its DT has cpu@0000..0005 and cpu@0100/0101. The trial submits the same firmware
CPU_ON targets:0..5 and0x100/0x101, retaining the stock SMC conduit and low,
firmware-owned16KiB entry allocation. Reserved memory regions are unchanged.

0075's prior console snapshot is preserved in out/logs/0075-before-m4. It has
maximum input callback102661us and maximum transfer100001us, versus0074's
approximately777ms callback. The user still reports very slow UI; this CPU
trial measures additional compute capacity rather than claiming touch/UI fixed.

## Topology and reset mapping

MAX_CPUS and matching mailboxes/GIC/provider/verification limits become8.
Loader publishes eight nodes and cpus=8. CPU reg properties retain real firmware
IDs; logical core IDs0..7 remain dense. A single symmetric scheduling domain
is retained for this first startup trial; no heterogeneous scheduler priorities,
CPU voltage/frequency policy, suspend, EL2 control or power-off path is added.
The four cache workers are retained for comparison.

Existing device evidence proves that this boot chain's ARM EL1 MPIDR reads
use Aff1 for the core index. The trial extends that byte-swapped shape to M4:
raw0x001/0x101 map to logical6/7 and firmware0x100/0x101. This M4 EL1 view is
still a hardware hypothesis until tested. Unknown shapes park and publish
raw MPIDR/MIDR before CPU-data lookup; they are never coerced into CPU0.
The initial stackless gate requires exact ARM or Samsung implementer/part and
rejects upper affinity. C initCPU independently rechecks raw identity against
the registered logical core. ResetHandler CPU-data lookup compares the converted
firmware ID; diagnostic mailbox indexing uses the separate dense logical ID.

The existing coordinator registers CPU0 first, then all secondaries, verifies
per-CPU timer interrupts and bidirectional SGI1/xcall on each CPU, and publishes
IOAllCPUInitialized only after all8 pass. The final required screen/log marker:
`8/8 CPUs initialized, per-CPU timer and bidirectional SGI1 verified`.
Missing entry, wrong identity, duplicate GIC targets or failed startup proof
stop with bounded waits before normal userspace startup.

## Checks

- Recovery identity observation is saved under out/research-0076.
- Host ASan/UBSan checks65536 low affinities, upper fields/flags, all three MIDR
  classes, invalid IDs and firmware/logical mapping roundtrips.
- Actual stackless entry identity gate executes in QEMU with register reads
  replaced by supplied test values:1310720 tests agree with C and reject
  unexpected upper affinity. This is instruction/control-flow proof, not M4
  hardware, cache-coherency or PSCI emulation.
- Actual provider/coordinator bodies pass40320 arrival orders, duplicate,
  incomplete topology, wrong coordinator CPU and scheduling-failure cases.
- Actual loader DT passes eight dense logical IDs, real firmware IDs, capacities,
  entropy/security metadata, reserved arenas and GIC target/SGI encoding tests.
- Existing RCpc/LDAPR compatibility semantics run through reference and native
  assembly host paths. Physical M4 already failed native LDAPR in the earlier
  recovery ISA probe; these tests do not claim hardware RCpc support.
- Patch0049 applies after the unchanged48 patches and exports the shared CPU
  affinity header for IOKit. Full production XNU and EFI builds are required.

## Installation / required next evidence

Only BOOT is updated after recovery/product/root/partition/capacity checks,
full rollback backup, installed0075 prefix match and exact0076 readback hash.
The working0075 personalized image remains private/ignored for rollback.
No factory WiFi address enters source or documentation, and nothing is pushed.
Firmware contains the newly built loader; SYS/USERDATA/recovery are not flashed.
Runtime still must establish M4 EL1 mapping,8/8 timer/xcall proof and sustained
userspace behavior. Successful compilation/flash does not establish those.
