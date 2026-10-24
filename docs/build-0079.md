# Build0079: separate startup acknowledgement from active timer proof

The0078 photo shows CPU3 incomplete with low=2/secondary=18. This is
before M4 startup. It does not identify which of initCPU, self-SGI or timer
was absent because0078 did not log those individual predicates.

The serial startup wait added in0077 incorrectly required a timer IRQ from
each newly started core before continuing. An idle CPU need not have a pending
timer deadline. Keep initCPU and real self-SGI/signal readiness mandatory;
check the timer later, in the existing proof phase that binds and runs the
coordinator on each CPU. The final per-CPU timer and bidirectional remote-xcall
proof are unchanged and still gate the8/8 completion claim.

Wait snapshots and timeout panics now include init, ipi and timer values.
The5s deadline, affinity rejection and serialized CPU_ON ordering remain.
The shared low entry can only be reused once C initialization and SGI handling
have completed. M4 identity and progress are still an on-device experiment.

The actual wait-body ASan/UBSan test covers ready/delayed entry, rejection,
missing C/SGI and an idle core with no timer IRQ. The actual coordinator test
passes40320 provider-arrival orders and existing failure cases. Production
kernel builds successfully;0078's corrected EFI is reused. BOOT-only flash
requires a full backup, expected0078 prefix and exact readback before reboot.
