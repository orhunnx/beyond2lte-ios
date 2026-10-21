# Build 0016: resident ARM64e compatibility PoC

## Scope

The plain ARM64 kernel now includes an opt-in EL0 compatibility service. It
handles the observed PAC/AUT/XPAC, authenticated control-flow/load and LDAPR
families, and supplies software QARMA5 PACGA. Scalar AArch64 assembly implements
the register/control-flow fast path and complete QARMA5 cipher. C validates
user accesses and commits saved register state transactionally. This does not
claim pointer-authentication security or advertise native PAC support.

The loader enables `b2l_arm64e_compat=1` with the existing USERDATA read/write,
unsealed-root selector. CPU count, 4 KiB kernel pages and protected memory/MMIO
ranges are unchanged. A separate translation cache is deferred to the next
phase; current unsupported instructions still take the exception path.

## Shared-cache pages

A B2L-only pager path assembles each 16 KiB logical slide page from separately
faulted 4 KiB physical pages, validates its chain, rebases in wired scratch and
publishes only the requested 4 KiB destination. The backing file is unchanged.
The 16 KiB CodeDirectory signature-admission gate remains unchanged and is a
separate follow-on requirement. No successful signature verification is invented.

## Verification

- Full native XNU build, normal assembly rules, linking, dSYM and stripping pass.
- Both scalar assembly helpers and the mixed-page pager are linked.
- 115 LLVM-generated opcode fixtures and all 4,096 LDRA combinations pass.
- 200,000-case native assembly/C differential tests pass under ASan/UBSan.
- QARMA5 passes 1,259 frozen known-answer/differential vectors on host and a
  PAC-free QEMU Cortex-A53 model, including x18 preservation.
- Actual-source runtime wrapper, grading and mixed-page pager tests pass.
- The pager negative control detects the prior mutable fault-info regression.
- All six patches apply from the pinned source, are idempotent and reject local edits.
- EFI, external full firmware and BOOT packaging/validation pass.

The BOOT image is 23,071,752 bytes, within the verified partition capacity.
Exact kernel/dSYM UUID: `F48871E5-D4AC-3918-A6AE-F60446A0941C`.
Local artifacts, hashes, symbols and receipts remain under ignored `out/0016`.

## Authorized device installation

BOOT was written without taking another backup. The entire image prefix was
read back and matched its SHA-256; reboot was accepted. USERDATA and SYSTEM were
not written by the installation operation. The first device photograph stops at early startup with an invalid ticket
lock. Exact 0016 dSYM lookup identifies the lock as `log_lock`. The new TUNABLES
startup message called normal printf before that lock was initialized, before
userspace or instruction emulation was reached. Host tests and readback do not
prove EL0 execution.

The existing unavailable-AMFI path and 16 KiB CodeDirectory handling remain
known follow-on gates. This build is a PoC, not complete iOS compatibility.
See [runtime design](arm64e-runtime.md) and [PACGA implementation](pacga-qarma5.md).

## 0016b correction

The TUNABLES message now uses the existing lock-free early framebuffer sink.
The boot-option parsing and instruction compatibility behavior are unchanged.
An actual-source regression test poisons normal printf during TUNABLES and
requires enabled startup to use the early sink; disabled modes remain silent.
The kernel is rebuilt and repackaged with unchanged 0016 EFI/FD inputs and
matching exact debugging symbols.

0016b BOOT was written and its entire image prefix read back with matching
SHA-256 before reboot. Exact kernel/dSYM UUID:
`587FB8BE-B4B7-3F47-84A6-1AE66929BDEC`. The user reports that the previous
invalid-ticket-lock panic is gone and kernel startup continues. No physical
EL0 ARM64e instruction-emulation or launchd success has yet been established.
Local artifacts and the installation receipt remain under ignored `out/0016b`.
