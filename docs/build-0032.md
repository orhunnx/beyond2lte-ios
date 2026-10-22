# Build 0032: two-core same-cluster hardware test

## Scope

Select CPU0 and CPU1 (MPIDR 0/1, Cortex-A55) using the local stock DT's
legacy SMC CPU_ON64 interface, 0xc4000003. No PSCI version response is assumed.
The Android recovery and all persistent partitions other than BOOT retain their
existing layout and contents. The kernel continues normal USERDATA HFS activity
and the existing private SYSTEM log-tail writes.

The physical entry is passed as 64 bits, including when above 4 GiB. Unexpected
EL, failed CPU_ON or incomplete startup/proof is not advertised as two working
CPUs. No fixed reserved-memory trampoline or firmware EL2 changes are used.

## Implementation and verification

Patch 0022 adds the physical secondary entry, avoids unsupported OSLAR access,
publishes existing reset data to PoC, discovers banked GIC CPU target masks, and
routes remote SGI1 to XNU. The IOCPU provider follows XNU 12377 registration
ordering without a duplicate processor_start. Its normal observer thread checks
secondary readiness, real per-CPU timers and bidirectional XNU cross-calls
before publishing gIOAllCPUInitialized. Waits are bounded; no IRQ wait occurs
inside masked initCPU.

A clean, separate MAX_CPUS=2 build regenerates all headers and assembly offsets.
The loader supplies exactly two CPU nodes and cpus=2. All 0031 RAM guards,
4024 MiB arena ceiling, 16 bootstrap pages, root-write policy, logger, launchd
cache admission and ARM64e compatibility behavior are retained. Translation
caching and crash-loop fixes are not part of this experiment.

Actual ADT and discovered-mask/duplicate/SGI tests pass under ASan/UBSan. The
0021-to-0022 upgrade, repeat application and local-edit refusal pass.

## Hardware test

The existing 0031 BOOT is the rollback artifact. Hardware startup and sustained
runtime require the next device screen and/or a recovery-extracted persistent
log. After 2/2 proof, use a short run to compare userland progress and inspect
scheduler, VM, TLB and UFS errors. A lack of a graphical UI does not by itself
identify an SMP failure; 0031 already had repeated SpringBoard/user-manager
crashes and substantial emulation overhead.

## Installed artifact

The clean full kernel and rebuilt EFI/FD complete successfully. Kernel inspection
and embedded-loader packaging validation pass. The 23,115,912-byte BOOT is
written and its complete prefix readback hash matches:
`f795ddf3ca1a67323a3cddf07108064984443abefb8c23c9f056775316ac84a5`.
Reboot is requested. Stripped/unstripped kernel and preserved dSYM share UUID
`0778C5BD-86AE-3C56-82E1-61F8A76A3E72`. These are installation/build checks;
actual CPU1 startup, the 2/2 proof and short sustained runtime are pending the
next device screen or extracted log.

## First hardware result: rapid reset, rolled back

The user reports a rapid crash/reset loop. Recovery extraction still shows the
previous 0031 epoch-1 log, byte-identical to the saved pre-test region: no 0032
log epoch was created. This provides no CPU_ON result, secondary-entry marker,
panic PC or 2/2 proof; the exact cause is unknown. Failure occurs before the new
persistent logger becomes active, so this test does not establish working SMP.
Do not attribute it to the high physical entry without a captured early error.

The complete previous 0031 BOOT is restored, its full image-prefix readback hash
matches the retained package, and reboot is requested. No filesystem image or
partition layout is rewritten during rollback. A further SMP experiment needs
an early screen/reset diagnostic before another userland performance comparison.
