# Build0081: discriminate measured M4 affinity using MIDR

## Device evidence

0080's supplied photo reports CPU6 low=2, secondary=0, init=0, ipi=0,
timer=0, with captured EL=1, MPIDR=0x80000100, MIDR=0x531f0030 and
high target0x900513000. CPU1..5 have stage18. The first M4's actual
EL1 affinity is0x100, not the previously hypothesized0x001. This overlaps
the established A55 CPU1 affinity; MPIDR alone cannot select CPU data.
The second M4's0x101 identity remains a trial until observed on device.

## Changes

Use MPIDR plus MIDR throughout local CPU/cluster lookup, initialization
validation and C/assembly checkpoints. ARM implementer0x41 with matching
A55/A75 part retains raw0x000..0x500 -> logical0..5. Samsung implementer0x53,
part0x003 permits only raw0x100/0x101 -> logical6/7. Firmware/DT IDs remain
0..5/0x100/0x101. Unsupported upper affinity and mismatched parts reject.

A shared stackless B2L_CPU_INDEX macro applies the same identity rules to
the early kernel gate, mailbox index and reset CPU-data firmware key. No
stack, function call, EL2 reconfiguration or reserved-memory change is added.
Patch0050 follows the existing49 patches and changes the pinned XNU source.

The early rejected-identity record occupies mailbox bytes64..87, but its
writer and reader maintained byte0's cache line. On64-byte cache-line cores
this could hide a genuine rejection behind a timeout. Both now maintain the
line atbyte64, containing all identity and rejection fields. Stage records
inbyte0's line keep their existing handling.

All8 CPU initialization, self-SGI, per-CPU timer and bidirectional remote
xcall gates remain. No8/8 runtime success is claimed from local tests.

## Verification

- Host ASan/UBSan checks65536 affinities x5 MIDRs, flags/upper affinities,
  firmware roundtrips and explicit raw0x100 collision (A55->1, M4->6).
- QEMU executes the actual identity macro, actual entry wrapper and reset
  firmware-key wrapper for1310720 inputs each and compares with the C rules.
- Production kernel builds with all50 patches. The corrected0078 EFI is
  reused. BOOT-only flash checks recovery/product/partition/capacity, full
  backup, expected0080 prefix and exact readback before reboot.
