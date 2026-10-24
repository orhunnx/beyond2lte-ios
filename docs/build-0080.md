# Build0080: make captured M4 identity visible in timeout panic

0079's supplied screen shows CPU1..5 at stage18 and CPU6 timing out with
low=2, secondary=0, init=0, ipi=0, timer=0. CPU7 was not started. Thus the
low trampoline ran its capture and reached its pre-branch publication, but
no high kernel-entry mailbox acknowledgement for CPU6 is observed. The photo
does not include the preceding WAIT snapshot with captured raw identity.

Include low-entry CurrentEL, MPIDR_EL1, MIDR_EL1 and the high target directly
in the formatted timeout panic, as a separate B2L LOW line. This uses values
already captured by0078's EFI; no EL2 configuration, affinity assumptions,
reserved memory, CPU_ON ordering, timer/SGI gates or low code change.

The actual wait-body sanitizer test now includes the shared ABI header and
formats real varargs. It asserts exact EL/MPIDR/MIDR/target values in timeout
output, in addition to existing startup, rejection and deadline cases.
Production kernel builds; corrected0078 EFI is reused. This is diagnostic,
not an M4 fix or8/8 runtime success. BOOT-only write retains full backup,
expected0079 prefix match and exact readback before reboot.
