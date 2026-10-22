# Build0045: reject affinity aliases before reset lookup

0044's first visible owner is EL1 MPIDR0x100. The nested data abort resolves to
__doprnt in our panic-format replay. Neither this identifies the original EFI
boot CPU nor proves a physical M4 ran: EL1's reported affinity and MIDR must be
observed together. Recovery Linux boots physical0 and its live DT uses PSCI
IDs0..5 for A55/A75 and0x100/0x101 for Cheetah.

There was an unsafe reset path: the secondary entry checked EL1 only, then the
HAS_CLUSTER0 reset scan compared Aff0 alone. An unexpected0x100 could therefore
select CPU0's active state.0045 rejects any reported affinity outside1..5 before
that scan, records rejected MPIDR/MIDR in unused diagnostic mailbox fields and
parks it. The coordinator reports this explicitly instead of permitting alias
execution. No sleeping-lock assertion or preemption counter is bypassed.

The panic path now captures typed first-fault message/PC/LR/ESR/FAR at the native
SLEH boundary. It renders those fields and the CPU snapshot without replaying
foreign panic varargs. The native panic handler retains its complete formatting.
Actual renderer ASan/UBSan tests include first-fault preservation, owner exclusion,
nested-owner refusal, retained-header wrap and memory bounds. Kernel build
succeeds. No broad optional test suite was rerun after the user's speed request.

Only BOOT was installed. Previous0044 prefix and complete0045 readback were
verified; SHA256=435a420ebe8f46249164f7d51b0f6103c884385329d5a70cd0fb636cda1b571a.
Kernel UUID=1AD1C9DF-BED8-3D8A-9F61-1000578C0A01. Reboot was requested. Reservations,
USERDATA and cache state were not changed; cache still has one worker.

## Device evidence

The clean0045 screen reports rejection of MPIDR0x500 with MIDR0x412fd0a1,
which is a Cortex-A75. The guard stopped before CPU-data lookup, the original
PC0/PRNG alias cascade is not visible in this capture, and the first panic is
the intentional coordinator rejection on MPIDR0. Thus the observed EL1 affinity
uses Aff1 for the core index, while firmware/DT PSCI targets are0..5. This is a
real mapping mismatch, not evidence that target5 booted an M4. Correcting the
board's EL1-to-target translation is the next step. Six-core runtime remains
unverified; guard success is not SMP success.
