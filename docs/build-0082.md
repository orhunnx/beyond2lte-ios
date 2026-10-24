# Build0082: eliminate implicit Apple A12 instructions from the kernel

## Evidence

0081's supplied photo shows the first M4/CPU6 reaches secondary-stage13
with EL1, MPIDR0x80000100 and MIDR0x531f0030. This proves execution through
arm_init_cpu, MMU setup, cpu_init and cpu_timebase_init. initCPU and SGI
acknowledgements remain zero. CPU7 has not started yet.

Immediately after stage13, arm_init_cpu calls kptimer_curcpu_up. The actual
0081 object disassembly contains LDAPR w19,[x8] at that function's first
acquire load of kperf_status. The earlier physical instruction probe found
M4 does not implement LDAPR. This is a concrete instruction incompatibility
in the next execution path; the photo itself does not contain the fault ESR.
The complete0081 linked text contains326 RCpc LDAPR/LDAPUR instructions.

A minimal C probe with the actual Apple clang and target reproduces this:
-march=armv8-a+crypto alone still selects target-cpu apple-a12 and emits
LDAPR. Adding +norcpc to march alone also did not resolve the implicit CPU.
Explicit -mcpu=generic produces baseline LDAR. PAN is enabled explicitly
with -Xclang -target-feature -Xclang +pan, retaining copyio's real PAN use.

## Change and verification

Patch0051 sets B2L's explicit generic CPU plus ARMv8 crypto and PAN, keeping
the iOS target/ABI and the existing50 patches. The prepare script's missing
line continuation after0049 is also corrected, so0050/0051 are included in
its verified patch sequence. All51 patches verify on the pinned source.

A fresh output directory out/build-env-0082/kernel is used; no previous
Apple-target kernel objects are reused. tests/test-kernel-cpu-target.py
compiles C and C++ acquire32/64, atomic RMW and PAN reads with the actual
configured flags. It verifies generic target CPU, LDAR32/64, PAN and absence
of RCpc or LSE RMW in the probes. scripts/audit-kernel-rcpc.py scans actual
linked executable instructions; the0082 kernel must contain zero RCpc.
The actual kptimer object is inspected again to confirm LDAR replaces LDAPR.

The first fresh link retained one LDAPR in the separately built
firehose_buffer_stream_chunk_install. Rebuild libfirehose_kernel with the
same explicit generic/ARMv8-crypto target, preserving its inherited kernel
flags, then relink XNU. scripts/build-firehose.sh records that reproducible
step. Final linked text audit:326 RCpc instructions before, zero after.

No additional stage probes or bypass of kperf, GIC, timer, SGI or CPU proof
is added. Userspace arm64e emulation/cache remains unchanged. M4/8-of-8
runtime proof and sustained UI testing remain pending device execution.
Only BOOT is flashed after full backup, expected0081 prefix and exact
readback verification; SYSTEM/USERDATA and all reserved ranges are preserved.
