# Two-core hardware experiment (0032)

Build 0031 retains one CPU and is preserved as the rollback package. Build
0032 integrates the original candidate for MPIDR 0 and 1 in the same cluster.
It uses the board's legacy
64-bit SMC CPU_ON interface and implements remote GIC target masks. It also
avoids Apple-only secondary OSLAR access, waits for real secondary completion,
and requires per-core timer/self-SGI plus bidirectional XNU cross-call proof
before publishing two running CPUs.

The actual two-node ADT and GIC target-mask tests pass under ASan/UBSan.
The 22-patch upgrade, idempotence and local-edit rejection test passes.
A separate clean MAX_CPUS=2 build regenerates topology headers and assym,
preserving the 0031 RAM, logging and compatibility behavior. These checks do
not prove that firmware accepts the high physical secondary entry above 4 GiB. No address truncation or
fixed reserved-memory trampoline is permitted. The first hardware gate is
SMC CPU_ON returning zero and CPU1 reaching reset/MMU/initCPU. The observer
then tests real timer delivery on each CPU and XNU cross-calls in both
directions. Only after those tests may the 2/2 initialized line be emitted.

No CPU_OFF, suspend, hotplug or heterogeneous cluster policy is provided.
Installation and sustained runtime proof are recorded separately in the build
note; a successful build or CPU_ON return alone is not SMP runtime proof.

First device result: 0032 rapidly resets before creating a persistent log epoch.
No CPU1 or 2/2 runtime proof is available. The readback-verified 0031 single-core
BOOT has been restored. See the 0032 build note; the exact early failure remains
unidentified.
