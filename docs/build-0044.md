# Build0044: retain first panic and record secondary startup

0043 video identifies an instruction fetch abort at PC0 after CPU_ON returns
success, before the later PRNG mutex panic. This does not prove CPU1 completed
initialization. Its corrupt/interleaved output cannot establish the first
fault's CPU or branch target.

The stackless ARM64 startup records LR, x0 and SP alongside stages1..8, retaining
x0/LR/SP and using x16/x17 as scratch. C checkpoints9..16 cover entry, thread
installation, idle/CPU/timebase/GIC/timer initialization and secondary handoff.
Stage17 records the first idle thread before load_context;18 records the actual
restored LR and SP immediately before the context-switch return. IRQ state and
preemption counts are not changed. Secondary CPUs skip the boot-only bitmap
text marker.

The panic renderer atomically selects one MPIDR as output owner. Other CPUs'
characters are suppressed after selection. The first message line and stage
snapshot are placed in a retained header band; later register/backtrace output
wraps only below it. This uses no sleeping lock or new persistent storage.
The actual renderer host test covers owner exclusion, nested-owner refusal,
retained-header wrap and framebuffer canaries under ASan/UBSan.31-patch chain,
720 provider-arrival tests, scalar renderer/CPU/cache machine-code gates and
kernel build pass. Host tests do not prove hardware SMP or absence of every
panic-context failure.

Only BOOT was installed after checking the previous0043 prefix. Full0044
readback matches71dce975ba32418ebdd33376fba7829f9ded2fef007344bb0f6f35295029b148;
reboot was requested. Kernel UUID: 13CFC1BC-A2E6-363B-9822-C73E0BCEB13C. No USERDATA
recovery write, cache reset, reservation change or cache parallelism was added.

## Device result

The new screen reports MPIDR0x100 and a nested data abort in __doprnt, called
from the first-line formatter in b2l_early_panic. It does not contain a usable
checkpoint dump, so first-fault preservation is not yet demonstrated on-device.
The source has a concrete unsafe admission: b2l_secondary_entry checks EL1 but
not whether the full affinity is in1..5. With HAS_CLUSTER0 the following native
reset lookup uses only Aff0, so an unexpected MPIDR0x100 can select CPU0's data.
This is a candidate explanation for shared/coherently corrupted CPU0 state;
its exact hardware/firmware cause is not yet established. Recovery independently
boots Linux on physical0 and identifies CPUs0..3 as A55,4..5 as A75 and6..7 as
Samsung Cheetah. Do not relabel the photographed0x100 as the original EFI boot
CPU or claim the firmware target mapping is proven from this frame alone.
