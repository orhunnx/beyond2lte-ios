# Build 0015: populated USERDATA root and ARM64e boundary

## Device result

The populated Journaled HFS root on USERDATA mounts read/write through the
native UFS/BSD path. The console identifies the selected USERDATA root and
`load_init_program` attempts `/sbin/launchd`. Execution stops with **errno 86
(EBADARCH)**. This is distinct from the earlier empty-root ENOENT error.
No iOS userspace execution, Setup completion or activation is established.

The supplied userspace is iOS 26.6.2, build 23G90. Both `launchd` and `dyld` are
ARM64e Mach-O executables. Their original files, disassembly and image receipts
remain private local inputs and are not repository content.

## Why the current kernel rejects launchd

References below use the pinned XNU source with the B2L patches applied:

| Source | Contract |
| --- | --- |
| `bsd/sys/errno.h:228` | EBADARCH is errno 86. |
| `osfmk/arm/cpuid.c:92` | A build without `HAS_APPLE_PAC` reports ARMv8. |
| `osfmk/arm64/cpu.c:730` | ARMv8 maps to CPU_SUBTYPE_ARM64_V8. |
| `bsd/dev/arm/kern_machdep.c:88` | The V8 host accepts V8/ALL, not ARM64E. |
| `bsd/kern/kern_exec.c:2017` | A zero binary grade returns EBADARCH. |

The photo and static source path explain the current failure. This is not a
root-filesystem mount failure. No architecture-grade override was installed.

## Physical instruction checks

A temporary static Linux process ran from recovery RAM, with affinity pinned
in turn to each of the eight available CPUs. It read the Linux-exposed feature
register and executed two bounded instruction probes. SIGILL was caught inside
the process. The executable was removed afterward; no partition was written.
The reproducible source is [cpu-isa-check.c](../tools/cpu-isa-check.c).

| Recovery cores | PACIA x0,x1 | LDAPR from valid memory |
| --- | --- | --- |
| Cortex-A55, 0-3 | SIGILL | Correct value loaded |
| Cortex-A75, 4-5 | SIGILL | Correct value loaded |
| Samsung M4, 6-7 | SIGILL | SIGILL |

The common Linux HWCAP does not advertise PAC. Linux exposes a sanitized
ID_AA64ISAR1_EL1, with APA/API/GPA/GPI all zero. That register is not a raw
per-core EL1 hardware dump; the pinned instruction results provide the separate
execution evidence. LLVM disassembly verified that the probes contain the
intended non-HINT PACIA and LDAPR opcodes.

A core that accepts LDAPR is not thereby ARM64e-compatible. The current XNU boot
uses only its first CPU. A later SMP port must address the measured RCpc
instruction difference between clusters rather than advertise a uniform ISA.

## Actual binary requirements

A bounded LLVM scan of the executable `__text` sections found:

| Instruction | launchd | dyld |
| --- | ---: | ---: |
| RETAB | 1097 | 1516 |
| BLRAA | 58 | 569 |
| BLRAAZ | 74 | 144 |
| BRAA | 5 | 73 |
| BRAAZ | 8 | 23 |

These are static instruction sites, not runtime call counts. Additional PAC/AUT
instructions, pointer fixups and dependencies exist. The scan does not establish
whole-userspace coverage or predict boot time. The `launchd` import stubs
contain another 589 BRAA sites outside `__text`. Both executables use
ARM64E_USERLAND24 chained fixups with 16 KiB starts pages; accepting that format
and its mapping requirements is separate from executing its instructions.

Some PAC instructions use the backwards-compatible HINT space. Authenticated
branches and returns do not all have that behavior. Treating every PAC-family
instruction as a no-op cannot preserve branches and returns. See the
[Linux architecture overview](https://www.kernel.org/doc/html/latest/arch/arm64/pointer-authentication.html)
and [LLVM pointer authentication model](https://llvm.org/docs/PointerAuth.html).

## Remaining compatibility work

A working ARM64e userspace port requires a coordinated implementation:

1. **Instruction execution:** cover authenticated branches, calls, returns,
   data operations and HINT-space operations consistently. A generic undefined
   instruction handler alone does not see HINT operations that already act as
   no-ops. The current `osfmk/arm64/sleh.c:1185` has no PAC emulator.
2. **Dyld and cache relocation:** implement authenticated chained fixups and
   correct shared-region bookkeeping. The authenticated pointer writeback in
   `vm_shared_region.c:3326` and `:3545` is compiled out without ptrauth calls;
   simply accepting the CPU subtype leaves encoded chains unprocessed.
3. **Task/thread and ABI contracts:** supply coherent pointer/key state,
   initial thread state and kernel/user pointer handoff. Review the guarded
   paths in `kern_exec.c`, `machine_routines.c`, `vm_shared_region.c` and
   `bsd/vm/vm_unix.c`. Native PAC macros also affect the kernel itself and
   cannot be enabled as a substitute for these implementations.
4. **Signed execution:** provide the real entitlement/signature services needed
   by this userspace. The existing absent-AMFI path in
   `bsd/kern/ubc_subr.c:4437` returns EPERM. Architecture acceptance does not
   supply those services.
5. **Evidence:** verify instruction semantics using synthetic programs before
   testing dyld, then prove that PID 1 executes on the device. Root mounting
   and successfully copying an IPSW are separate milestones.

The generic development `bingrade` option changes executable selection only.
No grade-only build, Mach-O subtype relabeling, or claim of native ARM64e support
has been shipped in response to this failure. BOOT 0015 remains installed and
recovery is available. The next installable userspace build is not ready.
