# Build 0050: product registry containers and unencrypted HFS class queries

## Evidence and corrections

0049's saved device crash trace shows libimage4 failing with
`panic: failed to get product node`. The actual caller opens
`IODeviceTree:/product`, then `/chosen/manifest-properties`. Neither existed
in the board-generated device tree. Add both containers, with the actual
Samsung product/model/board strings in the product node. The manifest container
is empty. No Apple CPID, board ID, unique device ID or signed manifest is
invented. The optional `/chosen/asmb` node remains absent. Resolving these node
lookups is not proof of full Image4 support on Exynos; later chip requirements
may still fail and must be checked in a new trace.

The container frameworks use protection-class queries while examining paths.
The port builds HFS with CONFIG_PROTECT=0. This previously compiled out
va_dataprotect_class support, so native F_GETPROTECTIONCLASS returns ENOTSUP.
Upstream CONFIG_PROTECT HFS explicitly returns class 0 for c_cpentry == NULL.
Preserve that existing unencrypted query contract in the B2L build: return 0
and mark only that requested attribute supported. Native protected builds keep
the original class expression; inactive queries and non-board builds retain
their behavior. No encryption, key wrapping, class-changing behavior or encrypted
filesystem capability is introduced.

The source staging script first validates the full existing provider manifest,
then applies this bounded known source edit and updates the tracked hashes and
compatibility revision. A fresh stage applies the same transformation before
publishing its manifest. Unmanaged or changed sources are still refused.

The live catalog shows some application containers created by MCM, while the
committed System and SystemGroup directory snapshots have no children. MCM
migration status files and its databases exist. A dirty HFS catalog snapshot
cannot rule out newer journaled entries. Thus complete MCM failure, or that this
attribute mismatch alone causes every container abort, is not established.
This fixes a concrete interface mismatch; lsd, ManagedSettings, AppProtection,
mobileactivationd and passd stability still require the next device capture.

## Checks and installation

- Actual staged HFS attribute block passes ASan/UBSan in all four combinations
  of CONFIG_PROTECT and B2L: class 0, native class 6, null native CP entry,
  inactive attribute and non-board behavior.
- Existing HFS provider, mount guards and root alias selection tests pass.
- Loader tests pass: actual DT decoding confirms 18 nodes, one product and one
  empty manifest-properties node, no fabricated Apple identity properties,
  exact six-core topology and every truncated output capacity. Arena reserves,
  framebuffer progress and GIC target tests pass.
- Kernel and complete embedded-loader firmware builds succeed. Stripped,
  unstripped and dSYM UUID: 51884A13-4162-342A-93D2-BF4F0FEEFCC4.
- BOOT is 23,190,552 bytes; SHA256:
  `804a41229f74dfffda9e1fb4600c088349dc289ae0c28eb443cb08ef71034751`.

Matched the current complete 0049 BOOT prefix, wrote only BOOT and verified
its complete readback hash. USERDATA's 4096-byte header matches before/after;
reboot was requested. The expanded SYSTEM cache and its current durable records
were not rewritten. The retained 0049 BOOT is the matching V2 rollback.
There is no 0050 runtime proof of crash resolution yet.
