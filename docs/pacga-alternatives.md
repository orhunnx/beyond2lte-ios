# PACGA alternatives investigation

## Hardware evidence

The previous recovery probe executes PACIA and LDAPR on each core. PACIA raises
SIGILL on all eight cores. The reported ID_AA64ISAR1 GPA/GPI fields are Linux's
sanitized common values, not raw per-core EL1 readings. Generic PACGA support
is a separate feature: direct per-core PACGA execution has not yet been tested.
Do not infer its absence solely from the PACIA result.

Exynos9820 has SSS9.0 hardware for AES/TDES, SHA-family hashing, HMAC/CMAC and
DRBG. The published algorithm capabilities do not document a QARMA/PACGA
service. They do not establish a direct replacement for a PACGA operation.
A PUF/key manager provides key services, not evidence of PACGA acceleration.

Primary sources:
- https://csrc.nist.gov/projects/cryptographic-algorithm-validation-program/details?product=10932
- https://semiconductor.samsung.com/processor/mobile-processor/exynos-9-series-9820/
- https://www.kernel.org/doc/html/latest/arch/arm64/pointer-authentication.html

## SipHash-2-4 candidate

Pinned XNU already has BSD-licensed bsd/net/siphash.c. SipHash-2-4 is a keyed PRF
for short inputs; a PACGA observation has two64-bit operands, fitting a16-byte
message and128-bit boot key. Its scalar add/rotate/XOR operations fit the
existing general-register-only kernel restriction. The adapter could keep only
the upper32 output bits, matching the architectural PACGA result shape.

This is a different result algorithm, not QARMA-compatible instruction
emulation. It could only be an explicit experimental B2L development ABI,
selected before the first userspace instruction and stable throughout a boot.
Creating and checking CFInfo must use the same algorithm/key across all tasks
and CPU migrations. Existing live signatures cannot survive a mid-boot change.
Broader userspace assumptions and persisted authenticated data remain untested.
Arm permits implementation-defined PAC algorithms; this does not establish that
any particular replacement is automatically architecture- or Apple-compatible.

Primary sources:
- https://cdn.kernel.org/doc/html/latest/security/siphash.html
- https://eprint.iacr.org/2012/351.pdf
- https://documentation-service.arm.com/static/644c18cbaa78c007af74fa2c?token=

## Local arithmetic comparison

The private host benchmark compiles the actual XNU SipHash source with only
header/platform adapters and -O2 -mgeneral-regs-only. It compares its16-byte
input with production scalar QARMA assembly, using synthetic public keys and
changing operands,500,000 iterations per algorithm in five runs. Both a SipHash
known answer and the independent QARMA paper answer are checked.

Median on this Apple Silicon Mac: QARMA5 149.034 ns, XNU SipHash-2-4
14.338 ns: about 10.39 times faster for hash arithmetic alone.
This is not a Samsung benchmark and excludes exception entry, VM validation,
register-context copying, mutexes and result-cache behavior. It is not a
prediction of UI or startup speed. These measurements preceded the0090 trial.

## Darwin assembly regression found and fixed locally

The previous LOAD_CONSTANTS macro used `adrp ...; add ...` on one expanded line.
Darwin assembler treats the semicolon as a comment; the ADD did not appear in
the real Mach-O object. QEMU's ELF assembler accepted both instructions, so the
existing ELF vectors did not detect it. The native Mac assembly returned
0x2905e47000000000 for the paper vector instead of0xc003b93900000000.
The missing low12 address add makes the old helper read page-base bytes rather
than its intended constants. The current-boot key is private/random, but that
does not excuse incorrect cipher math.

The source now emits ADRP and ADD on separate lines for both targets. The new
native Darwin test compares the production assembly with C and all1259 frozen
vectors successfully. scripts/test-arm64e.sh invokes it on Darwin arm64 in
addition to the existing ELF/QEMU tests. This source correction has not yet
been installed at the investigation checkpoint; it is now included in0090.
It fixes correctness, not the measured trap rate.

## Practical order

1. Retain the explicit QARMA fallback and its corrected code. Remove the
   steady-state immutable-key mutex and measure bounded exact-result caching.
2. Test a separately selected SipHash-2-4 development mode from a fresh boot,
   with the same128-bit key scope, consistent output truncation and rollback.
3. Measure traps/second, time spent calculating, cache hits and actual startup
   latency. Choosing another hash alone does not remove the PACGA exception.

## Implementation follow-up

[0090](build-0090.md) implements the explicit boot-selected SipHash trial,
immutable-key acquire/release publication and QARMA fallback. The earlier
measurements above remain host-only evidence; device results are tracked in
the build note. No result cache is added in this experiment.
