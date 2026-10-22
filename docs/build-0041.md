# Build0041: ordered CPU registration and finer early diagnostics

## Evidence and change

0040 resets quickly. Its captured frame shows asynchronous CPU0..5 registration
begin messages without a visible completion/CPU_ON proof. Its recovered disk
log is exactly the previous0039 epoch12 capture, not a new SMP log. No new
panic PC or reset cause is available; this does not prove that all later
instructions were never reached. It also does not prove a cache/PRNG defect.

B2LCPU now separates asynchronous IOService provider arrival from processor
registration. Each validated provider is published once with release/acquire
ordering. Only the sixth unique provider schedules a coordinator. That thread
announces the already-known six-CPU topology, then registers CPU0 first and
CPU1..5 in order. It holds no new mutex across ml_processor_register. Duplicate
providers, an incomplete topology, unexpected current CPU or thread creation
failure are errors rather than a successful online claim.

Native registration markers bracket entry, SMR worker initialization, per-CPU
PRNG registration and processor_boot. Scheduler markers bracket its locks,
idle thread, IPC and boot-CPU binding/start. These bounded early-screen markers
are present in the packaged kernel. They do not require working UFS logging.
The original XNU operations and their checks are retained; markers do not bypass
SMR, entropy validation, scheduler initialization or a failed CPU_ON.

The change removes uncontrolled provider-registration order, which is a startup
risk. It is not yet a demonstrated explanation or cure for the rapid reset.
The exact stopped stage must come from the next device result.

## Verification and installation

The actual provider-publication and coordinator bodies pass all720 arrival
permutations under ASan/UBSan with deferred host scheduling, plus duplicate,
wrong CPU, incomplete topology and thread-start-failure cases. This is an
ordering test, not an emulation of XNU locks, PSCI, timers or hardware SMP.
Actual29-patch upgrade/idempotence/local-edit refusal and existing ADT, GIC and
arena tests pass. The six-CPU build directory retains its regenerated offsets;
only affected native/board sources are rebuilt. Kernel, changed-object times,
scalar-only cache/emulator codegen, symbols and embedded-loader checks pass.

Only BOOT was written after verifying physical0040 against its retained image.
The complete0041 image-prefix SHA256 readback matches, and reboot was requested.
Known single-core0039 remains the rollback package. No recovery filesystem
mount, raw USERDATA mutation, reservation change or cache clearing occurred.
Six-core timer/xcall proof and sustained runtime remain unverified. The cache
worker remains single-threaded until that hardware gate is met.

## Hardware frame result

The next frames confirm CPU0 registration/initCPU returns, followed by CPU1 SMR
and PRNG completion, processor_boot, idle-thread and IPC preparation. CPU_ON is
reached with target1, context0 and high entry0x900506000. No returned result or
secondary-stage proof is visible before reset. The ordering change allows more
precise localization but six-core operation is still not established.
