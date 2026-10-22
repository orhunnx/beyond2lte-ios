# Build0043: CPU startup context isolation

## Device evidence

0042 reaches a kernel panic rather than the previous silent CPU_ON reset.
The photographed message is `Attempt to take mutex 0xfffffff9018d0858 with
preemption disabled (1) @lock_mtx.c:184`; the address is the user's exact
transcription. The available stack includes finishStartup+0x240 in the CPU
registration loop. Interleaved output does not identify the complete caller
chain or prove the offending mutex owner. It does not establish six-core SMP.
IOCPU::setCPUState only assigns state and is not evidence of a sleeping lock.

## Changes and limits

The CPU provider now records target/current CPU, current thread, IRQ state and
preemption level at registration entry, native registration return, IOService
publication begin/return, initCPU entry/return and CPU_ON entry/return. The
normal coordinator-only boundaries reject non-sleepable context before entering
another sleeping operation. Secondary init and CPU_ON are observation-only;
they are allowed to execute with IRQs masked. No preemption counter is reset,
no IRQ state is forced and no mutex assertion is bypassed. Native SMR, PRNG and
processor_boot stage traces also include current CPU and context.

B2LCPU is compiled with -mgeneral-regs-only, matching the existing exception
and cache paths. Actual0042 machine code used SIMD for info-structure zeroing;
0043 disassembly is gated to reject any SIMD/FP instruction in the CPU driver.
That code generation issue is corrected, but it is not a demonstrated cause of
the photographed mutex panic. This is a diagnostic boot, not a claim that the
panic or hardware SMP is fixed. Firmware and reservation map are exactly0042.
The six A55/A75 targets, M4 exclusion and timer/bidirectional-SGI proof remain.
Cache preparation still has one worker pending actual hardware SMP proof.

## Verification

The actual provider/coordinator host test passes all720 arrival orders and
failure cases. The actual30-patch sequence passes incremental29->30 apply,
idempotence and local-edit refusal. The low-entry allocation/assembly tests
pass. Kernel build, actual scalar codegen checks, Mach-O inspection and matching
stripped/unstripped/dSYM UUIDs pass. The final package verifies the unchanged
loader is embedded in the unchanged firmware. These checks do not prove runtime
SMP or a corrected hardware boot.

## Installation

Only BOOT was written after matching the connected device's0042 BOOT prefix
against its retained package. The entire0043 image prefix was read back and
its SHA256 matches5f6eb83148d086084dcbc547e10debe6ee7d40e1c0799697978dca1905501dcd.
Kernel/unstripped/dSYM UUID is31606B62-6FBA-334A-9DE0-4A98E8F22843.
Reboot was requested. No recovery USERDATA mount/write or cache modification
was performed. The known single-core0039 package remains available. A device
screen is still required to establish the new stopping point and any SMP proof.
