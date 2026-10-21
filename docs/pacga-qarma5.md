# Software PACGA with QARMA5

## Integration files

- `b2l_pacga_qarma5.h`: two explicit 64-bit inputs and explicit 128-bit key.
- `b2l_pacga_qarma5.c`: portable, original cell-array implementation.
- `b2l_pacga_qarma5_arm64.s`: full scalar AArch64 cipher implementation.

The C and assembly return `QARMA5(data, modifier, key_hi || key_lo) &
0xffffffff00000000`. The high key half is the whitening key; the low half is
the core key. Neither function reads or writes architectural key registers,
allocates memory, accesses a device, or generates a key. The caller must keep
key selection stable for the required signing/verification lifetime.

The AArch64 implementation covers the entire cipher, including cell
permutations, nonlinear substitution, diffusion, tweak forward/inverse steps,
round constants, key derivation and final 32-bit truncation. It uses general
integer registers only, preserves x19-x29 and SP, and does not touch x18,
SIMD/FP, PAC instructions or system registers. The symbol is selected for
Mach-O or ELF by preprocessing. Stack use is 80 bytes. The substitution uses
a packed immediate and variable scalar shifts; it performs no secret-indexed
memory lookup. Hardware timing/side-channel properties have not been audited.

Compile the assembly with the same freestanding AArch64 target as the kernel.
For portable C outside the Darwin ABI, reserve x18 explicitly if the platform
uses it (`-ffixed-x18`); use `-mgeneral-regs-only` for kernel compilation.

## Verification

`bash scripts/test-arm64e.sh /path/to/prepared/xnu` checks 1,259 frozen cases. It builds/runs portable C on the
host, compiles C and assembly for Darwin ARM64, then compares the full C cipher
and both PACGA projections in a bare-metal QEMU Cortex-A53 process. It also
checks that x18 is unchanged. The Cortex-A53 model lacks PAC instructions, so
this verifies the software fallback does not need them.

The first known answer is independent of this implementation and QEMU:

```
P      = fb623599da6e8127
T      = 477d469dec0b8762
key_hi = 84be85ce9804e94b
key_lo = ec2802d4e0a488e9
cipher = c003b93999b33765  (QARMA64_5, sigma2)
PACGA  = c003b93900000000
```

Source: Roberto Avanzi, *The QARMA Block Cipher Family*, IACR ePrint
2016/444, Appendix A.1: https://eprint.iacr.org/2016/444.pdf

The remaining frozen cases include zero/all-ones, every single-bit change of
data/modifier/both key halves relative to that known answer, and 1,000
pseudorandom public test cases. Expected full cipher values were produced by
a separately compiled upstream QEMU packed-word reference. Another 2,259-case
host differential run also passed. The reference generator was used privately; the frozen test data is self-contained.

QEMU source used as reference:
https://raw.githubusercontent.com/qemu/qemu/master/target/arm/tcg/pauth_helper.c

Downloaded source SHA-256:
`3848687fe9bfca20c7907f600f917d1d96e12dd210e88316891cf913b8067a60`.
Its generic-authentication helper explicitly masks off the low 32 result bits.
The algorithm selects sigma2 and five forward rounds for PACQARMA5.

## License and provenance boundaries

The integration C, header and assembly are newly structured implementations
of the mathematical construction and carry BSD-2-Clause notices. QEMU was
consulted as a behavior oracle; this is not a claim of clean-room isolation.
No QEMU source text is included in the integration files.

`qemu-pauth-reference.c` and `qemu-reference-adapter.c` are separate temporary
LGPL-2.1-or-later reference artifacts retaining QEMU's original notices. They
must not be copied into BSD-marked kernel files or silently included in the
integration commit. Distributing those reference sources requires preserving
their license; statically linking LGPL code would raise separate license
compliance obligations. The production implementation does not link them.

`test-vectors.inc` contains synthetic public inputs/keys and scalar expected
outputs, not Apple binaries, actual device keys or captured identifiers.

## Limits

- Correct QARMA5 math is not proof of compatibility with Apple's
  implementation-defined hardware algorithm or its real key material.
- These helpers implement generic PACGA only, not instruction decoding,
  exception return, PAC/AUT pointer encodings, authentication or key isolation.
- The caller's software-key generation, scope, storage and lifecycle still
  need integration review. Host/vector/QEMU tests do not establish device boot
  or iOS userspace success.
