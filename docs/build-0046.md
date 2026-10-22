# Build0046: translate observed EL1 Aff1 into PSCI CPU IDs

## Evidence

0045 rejected EL1 MPIDR0x500 with MIDR0x412fd0a1, an A75, while the firmware
CPU_ON targets remain0..5. Recovery's real Linux DT and pinned upstream DT both
use0..5 for A55/A75 and0x100/0x101 for Cheetah. The source's MPIDR register reads
are not a static swap implementation. Thus distinguish the observed EL1 view
of this boot chain from Linux/firmware IDs; the reason for that difference in
firmware/EL2 state is not yet proven. Do not call the earlier0x100 panic owner
an M4 without MIDR evidence, nor call the original EFI boot CPU0x100.

## Correction

The native reset lookup, raw local CPU/cluster register readers, per-CPU
checkpoint writer and assembly mailbox indexing now translate EL1 Aff1 into
the board's0..5 CPU IDs. The normal ml_get_cpu_number API retains its firmware-ID
input contract; only its local raw-MPIDR callers normalize their input. PSCI
CPU_ON arguments and EFI DT are unchanged. This avoids both CPU0 alias lookup
and invalid local CPU indexing after entering C.

Before touching CPU data, the stackless entry requires Aff0/Aff2/Aff3 zero,
Aff1 in1..5, implementer0x41, and the expected part: d05 for CPUs1..3, d0a for4..5.
CPU0 remains the already-running boot A55. Samsung Cheetah/M4 identities are
rejected. The rejected-affinity park and typed first-fault snapshot remain.
No EL2 register, firmware reservation, CPU_OFF, suspend or DVFS change is made.

The actual C mapping helper passes all65536 low affinities, allowed status bits,
upper-affinity rejection and A55/A75/M4 identity checks. Kernel build succeeds;
modified-object timestamps and stripped/unstripped/dSYM identities match.
Kernel UUID: 98FB6B9C-FDBD-3BEA-9F35-26F8EC557FD1. The user requested rapid installation,
so no broad optional suite was repeated. These checks are not runtime SMP proof.

## Installation

Only BOOT was written, after matching0045's device prefix. Full0046 readback
matchesf090c6326c3750569ba8893f4f2d1613fc2e0ec5c3397e2dbfb1c33bb248063d and
reboot was requested. Six-core timer and bidirectional-SGI proof is still
required on-device. The native cache still has one worker pending that proof;
USERDATA, cache records and firmware are unchanged. Known0039 rollback remains.
