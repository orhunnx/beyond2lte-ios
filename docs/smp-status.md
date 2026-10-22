# Two-core follow-up: isolated candidate, not installed

Build 0031 deliberately retains the registered boot CPU. A separate original
candidate describes MPIDR 0 and 1 in the same cluster, uses the board's legacy
64-bit SMC CPU_ON interface and implements remote GIC target masks. It also
avoids Apple-only secondary OSLAR access, waits for real secondary completion,
and requires per-core timer/self-SGI plus bidirectional XNU cross-call proof
before publishing two running CPUs.

Four affected ARM64 translation units compile in isolation. Synthetic ADT and
GIC target-mask tests pass under ASan/UBSan. These do not prove that firmware
accepts the high physical secondary entry above 4 GiB. No address truncation or
fixed reserved-memory trampoline is permitted. A clean MAX_CPUS=2 build must
regenerate topology headers and assym; the new RAM/log changes must be preserved
when rebasing the isolated source. The full package and physical startup tests
remain outstanding. The candidate is not part of the installed 0031 image.
