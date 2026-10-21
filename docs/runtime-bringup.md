# Exynos 9820 first-boot runtime

The B2L target is plain ARM64, ARMv8-A with crypto extensions, 4 KiB pages,
without Apple pointer authentication, BTI, Apple MMU monitors or secondary CPUs.
It runs only the CPU on which firmware launches the EFI application. It does
not assume that this CPU has MPIDR 0; the loader records the actual MPIDR.

## Display and progress evidence

Build 0002 reached XNU assembly on the device: the user confirmed its separate
green entry marker. Build 0003 visibly reached C initialization and MMU
transition milestones. Build 0005 completed VM bootstrap and printed through
the framebuffer serial sink, then faulted at the missing allocator RNG provider.
Build 0006 registered the HMAC-SHA256 kernel PRNG and continued into later
startup, where a skipped stock console attachment left a zero ticket lock.
Build 0008 physically verified the boot CPU at MPIDR 0, EL1, IRQs enabled:
`1/1 CPU initialized`; timer counts advanced 43 → 45 → 47, SGI1 stayed 1,
and the dedicated proof reported `scheduler awake`. Initial kernel entry, VM,
CPU registration, interrupt delivery and timed scheduling are therefore device
verified. BSD startup later stopped at its missing pthread provider; this is
not a complete operating-system boot.

The kernel owns the text renderer. It follows the A5 port's early-console and
framebuffer serial-sink lifecycle, using the existing firmware GOP geometry
from `boot_args` rather than any A5 addresses. Its glyph bitmap is upstream
XNU's open-source `iso_font`, not a font extracted from an IPSW or KDK.

The display controller and its original reserved physical window remain intact.
No GPU, panel, clock or power-domain reprogramming is needed to draw CPU text.

| Visible milestone | What it establishes |
| --- | --- |
| Separate green rectangle near the bottom | First XNU instructions executed before system-register initialization |
| `XNU ENTRY` | Stackless assembly font rendering before MMU setup |
| `MMU PREP` | RAM and bounded GOP bootstrap mappings were installed |
| `MMU ON` | Execution continued after enabling the MMU |
| `ENTER C` | Assembly is about to branch to the C bootstrap |
| `B2L XNU C ENTRY` | `arm_init` entered with a usable early framebuffer |
| `B2L XNU 1..17: ...` | C bootstrap, VM transition and serial-sink milestones |
| `B2L EARLY PANIC: ...` | Panic formatting before ordinary KPRINTF initialization |
| `B2L GIC: GICv2 IRQ ready` | Controller register programming only |
| `B2L CPU: 1/1 CPU initialized` | Boot CPU registered with the scheduler |
| `B2L IRQ proof: timer=... SGI1=... scheduler awake` | Real interrupt delivery and timed scheduler wakeup; both counters must be nonzero |

Assembly text starts at y=336, and C text around y=480, so a cropped photograph
need not include the bottom marker. Text pixels are genuinely expanded 3x at
1440 pixels (24x48 cells), or 2x at widths 800 through 1199. The early renderer
wraps through text rows and clears the next row, avoiding a large scroll copy.

The output-only framebuffer sink registers before the stock serial-device
phandle checks, then carries both `printf` and `kprintf`. It does not invent
UART input, receive interrupts or a keyboard. No late stock video-console
acquisition clears the early stage history.

These milestones must be observed on the physical device. Compilation and
symbol inspection alone do not establish physical execution or interrupt delivery.

### Mapping lifetime and unchanged physical guards

The original firmware `DisplayReserved` region is `0xCA000000..0xCB400000`
(20 MiB). The 1440x3040, stride-5760 visible buffer ends at `0xCB0B3000`.
Its 2 MiB-aligned mapping ends at `0xCB200000`, inside that existing guard.
Both bootstrap assembly and the final mapper reject any rounded GOP extent
outside the original window. These pages are never advertised as normal RAM.

Two extra static bootstrap table pages reside inside the kernel image. They
map the already reserved GOP through TTBR0 without increasing the managed RAM
arena. Before TTBR0 is removed, XNU installs the same bounded device-memory
mapping at a reserved kernel VA below `LOW_GLOBAL_BASE_ADDRESS` and relocates
the console pointer. That virtual gap is already excluded from the allocator
by `pmap_virtual_region(3)`. The physical framebuffer remains the same.

## Interrupt controller and clock

The hardware addresses come from the existing Exynos 9820 device tree:

| Resource | Physical address / value |
| --- | --- |
| GICv2 distributor | `0x10101000`, length `0x1000` |
| GICv2 CPU interface | `0x10102000`, length `0x1000` |
| Architected virtual timer | PPI / INTID 27 |
| Kernel CPU signals | SGI 1 |
| Expected counter frequency | 26 MHz; read from `CNTFRQ_EL0` at runtime |

The implementation uses non-secure Group 1 IRQ delivery. The IRQ dispatcher
masks the GICv2 IAR with `0x3ff`, preserves SGI source bits for EOI, ignores
special acknowledgements 1020 through 1023, routes INTID 27 to `rtclock_intr`,
and routes SGI 1 to `cpu_signal_handler`. Combined EOI/deactivation is selected.

Before enabling these sources, stale non-secure firmware peripheral SPIs and
unused banked PPIs are masked. Secure-world ownership is preserved; accesses to
secure-only interrupt bits remain subject to the GIC's non-secure restrictions.
Unexpected enabled peripheral IRQs panic with the interrupt number because this
first-boot target has no peripheral interrupt drivers.

The timer uses `CNTV_*` registers, so no fake MMIO timer node is supplied.
`CNTFRQ_EL0` initializes XNU's timebase and decrementer frequency. CPU, bus and
memory clock frequencies remain unknown (zero); they are not invented from
the independent architected counter frequency.

## Apple device tree contract

All numerical values in this Apple device tree are native little-endian values,
not flattened Linux device-tree cells.

- Root compatible: `samsung,beyond2lte`.
- `/arm-io`: `device_type = "soc"`; `ranges` is three 64-bit values with
  the second equal to physical SoC base `0x10000000`.
- `/arm-io/gic`: `reg` is four 64-bit values:
  `[0x101000, 0x1000, 0x102000, 0x1000]`.
- `/cpus/cpu0`: `device_type = "cpu"`, `compatible = "b2l,boot-cpu"`,
  `state = "running"`, 32-bit `reg` containing the executing CPU's masked MPIDR.
  Cluster type is SMP. Only one CPU node is present.
- Boot video: 32-bit GOP pixels with valid stride/dimensions. The physical
  framebuffer address carries bit 0 set, the XNU physical-address indicator;
  the assembly marker masks the low two bits before access.

## Deliberate first-boot limits

The platform expert publishes the CPU without requiring Apple NVRAM. The CPU
driver follows XNU 12377's `ml_processor_register` behavior, which already calls
`processor_boot`; it does not start the boot CPU a second time.

PSCI secondary CPU startup, suspend/resume, storage drivers, USB, input,
GPU acceleration, iOS user space and writable root filesystems are outside this
first-boot runtime. Missing drivers are not represented by success-returning
stubs. An expected later inability to locate a root filesystem must be reported
as such rather than as a completed operating-system boot.

## Software text console checks

The B2L scroll path copies exactly the visible byte width per physical row,
using overlap-safe order. It avoids the generic open-source ARM scroll helpers,
which multiply an already byte-based pointer difference by four.

`python3 tests/test_console_raster.py /path/to/patched/xnu` extracts the actual
B2L glyph, cursor and scrolling functions, compiles a host harness with
AddressSanitizer/UndefinedBehaviorSanitizer, and checks 2x/3x raster expansion,
last-cell bounds, stride padding and both overlapping scroll directions.
This checks software geometry; it does not establish that a device booted.

`python3 tests/test_early_console.py /path/to/patched/xnu` runs the actual early
renderer against host-backed buffers with ASan/UBSan. It checks ring wrapping,
carriage return, high-bit character indexing, stride-padding canaries, pointer
relocation and rejection of geometry outside the original display guard.
Only the MMIO pointer and architectural barrier are substituted in the harness.

## First-boot random services (build 0006)

The photographed 0005 abort was a NULL external corecrypto table dispatch from
`vm_map_store_crypto_init` into `crypto_random_kmem_ctx_size`. The B2L adapter
provides the allocator's explicitly noncryptographic, lock-free context contract
using independent SplitMix64 contexts seeded from real `early_random()` output.
Bounded output uses rejection sampling; this generator never supplies strong
random bytes for the rest of the kernel.

A distinct HMAC-SHA256 DRBG registers through XNU's kernel PRNG API at
`EARLY_BOOT/FIRST`, before the allocator's `EARLY_BOOT/MIDDLE` initializer. It
uses the DRBG and digest implementations already linked by upstream XNU, real
boot-derived seed material, serialized generation/reseeding, and the existing
entropy conditioner and health tests. No FIPS validation is claimed. Refresh
uses try-lock semantics and accepts the existing conditioner's complete output.
The original 256-byte EFI entropy requirement remains enforced.

`cc_rand_generate` and `random_buf` use the real early/registered DRBG when no
external provider exists. Full `size_t` requests are split into unsigned-sized
chunks. SHA-256 and SHA-512 wrappers similarly select existing built-in digests;
the latter is required by the live entropy conditioner and user reseeding.
These narrow adapters do not register a partial global corecrypto table.
At build 0006, AES, RSA, SHA-384 and other external-provider services were not
implemented. Build 0010 adds real AES CBC/ECB as described below; the target
still does not load a later external global crypto provider.

`tests/test_random_services.py /path/to/patched/xnu` compiles the actual adapter
bodies and locally supplied upstream crypto implementation under ASan/UBSan.
Only kernel startup/locking hooks are hosted; real pthread mutexes serialize
the same state. Independent Python HMAC-DRBG/SHA known answers verify initial
output, reseeding, entropy refresh and streaming digests. Tests also check
concurrent nonduplicate output, allocator vectors, independent seeds, unaligned
output canaries, bounded rejection and startup rank ordering. Tests passed for
0006; later device photographs confirm progress past that failure.

## Console control lifecycle (build 0007)

Build 0006's photographed invalid ticket lock was symbolized as `vcputc_lock`.
The early serial renderer had intentionally skipped stock `PE_create_console`
and `vcattach`, but later screen-control calls still used the stock ticket lock.
The progress lock and graphical backend callbacks had the same missing lifetime.

B2L now declares both console locks in XNU's standard `LOCKS` startup subsystem,
after the lock group and before KPRINTF or IOKit. The later legacy initializers
are excluded for B2L, preventing duplicate initialization. Ticket validation is
unchanged. Every existing ticket/progress lock consumer uses these same locks.

`PE_init_printf(TRUE)` again calls `vcattach`. B2L attachment initializes real
stock console geometry and all backend callbacks with the already bounded,
remapped GOP address from the early renderer. It does not create another physical
mapping or cache alias. It skips initial screen clearing, message-buffer replay
and progress acquisition. The existing serial scanout is recorded as acquired,
so the bootstrap thread's initial acquire preserves history. Explicit later
release, disable, pause, resume and acquire requests retain normal control paths.

`tests/test_console_lifecycle.py` executes the actual B2L attach and stock
init/enable/pause bodies under ASan/UBSan, with host lock validation, allocation
and backend dispatch hooks. It verifies preserved pixels on attach/disable,
usable callbacks, both lock lifetimes, no double initialization, pause/resume,
and a real subsequent enable/clear/free cycle. The early renderer's separate
regression test also verifies the mapping accessor before and after relocation.
Both tests passed. The 0007 device photo confirms console attachment without
the earlier invalid-lock panic, then reaches the Image4 initialization gate.

## Absent Image4/AMFI trust-cache provider (build 0008)

Build 0007 visibly completed console attachment, PRNG registration and later
startup before stopping at `image4 interface not available` in the trust-cache
runtime initializer. This is the same standalone-kernel dependency handled by
the A5 port's open-source `0015-sweet-unavailable-trust-cache-runtime.patch`,
reviewed as a design reference. Its later userspace XML entitlement/AMFI bridge
is a separate feature and is not imported into this first-kernel-boot target.

B2L now represents the absent service explicitly. If Image4, AMFI, a supported
trust-cache interface version or any required trust-cache callback is missing,
initialization records NULL runtime pointers and leaves a read-only-after-startup
readiness flag false. It installs no fake interface and never reports signature
verification or a trusted match. A complete provider supplied at startup keeps
the existing initialization and validation path. This is a one-time boot
initialization contract; late provider installation is not implemented here.

All eight public trust-cache APIs check readiness before provider, entitlement,
lock or Image4 parsing access. Queries, loads, unloads, UUID checks and capability
requests fail with `KERN_NOT_SUPPORTED` when unavailable; malformed required
arguments fail with `KERN_INVALID_ARGUMENT`. Query tokens, REM permissions and
capability outputs are cleared before returning. Static loading records that it
was attempted, then returns without accessing device-tree memory or loading any
cache. Counts stay zero. Absence never grants trust or makes an artifact valid.

`tests/test_trustcache_unavailable.py` compiles the actual first-executed guard
bodies and provider-completeness check under ASan/UBSan, replacing downstream
provider operations with a dispatch trap. It checks pre-init, absent, stale and
each missing callback; all eight APIs; NULL output arguments; poisoned input
addresses; cleared result data; and no downstream dispatch. A complete fixture
is allowed through the presence gate. This test checks interface safety, not
cryptographic signature verification. It passed for 0008. The 0008 device
photo confirms progress beyond trust-cache initialization and successful IRQ
proof, followed by the separate missing pthread provider failure. Physical
guards are unchanged.

## Real built-in libpthread provider (build 0009)

The 0008 device reached BSD startup and created the new AIO workqueue before
`pthread_init` correctly rejected a NULL provider function table. B2L now embeds
Apple OSS `libpthread-539.100.4`, the version listed by Apple's macOS 26.5 release
manifest, pinned to commit `1f4f5265b319111142f1bf3a27d4484ef5a98314`. The A5
port's `0019` staging/built-in provider and `0102` adaptation were reviewed as
precedents; their older 514 implementation and hardware assumptions are not used.

The real upstream `kern_init.c`, `kern_support.c` and `kern_synch.c` compile into
the B2L kernel. Registration runs at `EARLY_BOOT/SECOND`, before lockdown and
before BSD calls the unchanged `pthread_init` shim. That shim still calls the
provider's real initialization, which creates synchronization lock groups,
locks, process hash state, cleanup thread calls and sysctl state. No pthread
syscall, wait queue, thread creation or workqueue function is replaced by a
success stub.

The complete LP64 provider wait element is 56 bytes with alignment 8 and its
trailing `kwe_uth` at offset 48. B2L's kernel-side opaque storage now matches;
compile-time assertions enforce all three properties. All 61 callback names
used by the provider have real designated implementations in XNU 12377. The
upstream function table intentionally remains version 0: changing it to version
1 would select an unimplemented `bsdthread_register2` slot instead of the
provider's supported legacy registration function.

See [provider dependency and staging](libpthread-provider.md). Host ASan/UBSan
tests execute the actual registration table/start/current-thread helper and
`_pthread_init` sequence with hosted kernel services. They check storage bounds,
registration ordering, retained duplicate/missing-provider panics, callback
completeness and refusal to rebuild from edited staged input. The actual kernel
build and linked symbols confirm the complete provider is present. Device
The 0009 device photo confirms progress beyond pthread initialization into
DLIL work threads, then a separate AES-provider fault during TCP initialization.
Physical guards are unchanged.

## Real portable AES CBC/ECB (build 0010)

Build 0009 passed pthread initialization. Its next photographed data abort was
matched to the exact kernel's `aes_encrypt_key`/`aes_encrypt_key128`, reached
from TCP Fast Open initialization. The external global crypto table remained
NULL; returning a fake key-setup success would not implement the TCP cookie
operation. B2L now uses real portable bitsliced AES from MIT-licensed BearSSL
for the legacy CBC and single-block ECB interfaces, following the A5 port's
`0020` and `0098` precedents. No TCP/network initialization is disabled.

The cipher is pinned to `7bea48e5e850ab4cafbe68d3765cdaba13a86d6f`. Only its
five AES implementation files and required headers/license are staged locally;
none of that fetched tree is vendored into this repository. All six adapter/
cipher translation units and the legacy wrappers compile with general registers
only. They allocate no heap state and require no SIMD or CPU AES extensions.
Key contexts fit the existing XNU ABI and can be copied as network code expects.

AES-128/192/256 key lengths are accepted in bytes or the documented bit units.
Legacy CBC copies the caller's const IV, so it remains unchanged. A NULL IV
means zero IV, preserving the single-block ECB behavior used by TCP cookies.
Generic mode calls update their mutable IV normally. In-place encryption and
decryption and overlapping input/output use the real BearSSL implementation
after a safe `memmove`. Invalid keys invalidate the context; NULL required pointers, invalid contexts
and block-length overflow return an error.

There is no partial global crypto table. GCM is not implemented by this adapter:
all 18 legacy GCM APIs decline a NULL/incomplete provider or mode, returning an
error (or context size zero) without touching caller output. Optional IV helper
callbacks are checked separately. No authentication result or tag is invented.
RSA, AES-XTS and other unrelated provider services remain separate work.
MD5 was still missing in 0010; the 0013 HFS integration adds the real legacy
MD5 implementation described in [HFS provider details](hfs-provider.md).

`tests/test_aes_provider.py` compiles the actual adapter and legacy wrapper
bodies, real pinned BearSSL AES and upstream CBC dispatch under ASan/UBSan.
NIST SP 800-38A Appendix F vectors pass for 128/192/256-bit CBC and ECB encryption
and decryption, including bit/byte lengths, copied contexts, in-place and overlap
buffers, const and chained IV semantics, zero blocks, invalid keys, overflow and
all unavailable GCM APIs. These are implementation tests, not FIPS validation.
See [AES dependency details](aes-provider.md). The standalone build passed;
The 0010 device photo confirms AES/TCP initialization passed, followed by
Skywalk self-tests and BSD autoconfiguration (`bsd_autoconf: calling kminit`).
It then stopped at the separate missing CTRR-state handoff described below.
Physical guards are unchanged.

## Truthful CTRR-state handoff (loader-only build 0011)

Build 0010 stopped at the unchanged assertion in `ml_unsafe_kernel_text`: its
state initializer had never received the required firmware policy property.
The initializer runs at `TUNABLES/FIRST`, before VM/BSD startup, and accepts
`/chosen/kernel-ctrr-to-be-enabled` only when its byte length is exactly
`sizeof(int)` (four bytes on this target). Omitting the field does not mean
"disabled" to XNU; it leaves the state uninitialized and a later consumer asserts.

The loader supplies a native little-endian four-byte zero. B2L has no Apple
CTRR/KTRR hardware integration; zero truthfully classifies kernel text as unsafe
for this policy interface. It does not claim that Apple hardware protection
will be enabled. The kernel assertion and initialization logic stay unchanged.
This is a loader device-tree contract correction, using the same compiled kernel.

The inspected consumers are DTrace FBT/SDT restriction checks, optional CTRR
platform tests, and the PPL bootstrap when `XNU_MONITOR` is configured. The
current B2L target has neither Apple CTRR/KTRR/paravirtualized CTRR nor PPL/SPTM;
no associated protected-region property is required by this current build.
The policy value does not itself program a protection register, change normal
page-table read-only/execute permissions or expand physical RAM/MMIO guards.
The loader-only 0011 device photo confirms this assertion is gone. Startup
passes `kminit` and loopback setup without a reported panic, then waits in root
discovery for `IOMedia` with `Content = Apple_HFS`. This establishes the initial
XNU kernel boot milestone; it does not establish a mounted root filesystem,
completed BSD startup or userspace execution.

## Root filesystem prerequisites: read-only audit

The displayed `Apple_HFS` match is XNU's default root-search dictionary. It does
not establish that HFS or a storage driver is installed. Apple OSS HFS is separate
from XNU; the release-paired candidate is `hfs-715.120.4`, commit
`124b970fd5390f1c2cd6f985478616a04f6eeb1f` (annotated tag
`fa6f37df5ceb82c4149fa5caff89f75662457fc9`). An isolated source checkout was
reviewed, not compiled or integrated.

The A5 `0026` built-in HFS registration remains a viable design reference:
register the real filesystem/vnode operations with `vfs_fsadd` after `vfsinit`,
then initialize its zones, encoding and sysctl state. Current iPhoneOS HFS
configuration enables AppleKeyStore content protection; an initial unencrypted
HFS root needs an explicit unencrypted feature configuration and corresponding
guarded AKS glue. The old macOS-target feature assumptions cannot be copied.

A native BSD block device can use the A5/memory-device style instead of importing
all IOStorageFamily: real bdevsw/cdevsw operations, device nodes, bounded storage
geometry and an explicit root-selection path. Stock arbitrary `rd=` names still
wait for IOKit matching; only `mdN` has a built-in direct-device exception. Root
media helpers also currently assert that IOMedia/IOBlockStorageDriver classes
exist, requiring honest optional-family handling for a native device. The full
IOMedia route instead requires actual IOStorageFamily, media/BSD-client and
partition/transport services; publishing a class name alone is insufficient.

HFS requires logical block size/count ioctls and may temporarily select a 512-byte
view. A UFS backend must correctly translate that view over its physical sectors,
complete buffer I/O with errors/residuals and enforce the selected logical
partition's extent bounds. It must resolve the actual LU and logical extents;
a TWRP partition alias is not by itself an on-disk GPT identity. Read-only
metadata access and a clean HFS mount are separate milestones from write/journal
support. An existing memory disk can isolate filesystem bring-up from transport,
provided its pages remain within the unchanged reserved boot arena.

After a mount, this iPhoneOS-target kernel still requires `FSIOC_KERNEL_ROOTAUTH`;
a plain custom HFS volume does not provide Apple's sealed-root authentication.
That research-root policy must be explicit rather than falsely reporting a
verified seal. An empty HFS volume also contains no init/launchd userspace. No
filesystem integration, kernel build or device/storage writes were performed
as part of this audit.

## Native read-only UFS BSD adapter (build 0012)

The native disk adapter is initialized after BSD pseudo devices and before
`IOKitBSDInit`. It consumes the transport's actual READ CAPACITY geometry;
neither a device-tree capacity nor a recovery partition alias is treated as
storage geometry. The initial `/dev/b2lufs0` and `/dev/rb2lufs0` nodes represent
the selected physical UFS logical unit. Validated GPT partitions receive
`b2lufs0pN` and `rb2lufs0pN` nodes using their original one-based entry index.
These are native BSD devices, not an invented IOMedia service. Root matching
and the existing stable root-device wait are unchanged.

The portable decoder verifies both primary and backup GPT header CRCs, both
entry-array CRCs, header locations and reciprocal backup references. It checks
usable ranges, table bounds, matching metadata, entry-size/count limits,
nonempty partition GUID fields, partition bounds and overlapping extents.
Unknown or invalid GPT metadata produces no partition nodes. GUID values and
raw sectors are never printed. The short proof log reports geometry, validated
entry/partition counts, device readiness and the selected partition signature.

SYSTEM matching is case-insensitive; `super` is the fallback label. Ambiguous
matches are not probed. The probe recognizes HFS+/HFSX and ext4-family signatures
and checks Android LP geometry even when the GPT label is SYSTEM: a recovery
SYSTEM alias can refer to a dynamic-partition container. A signature identifies
metadata only; it is not a filesystem validation, mount or logical-partition
implementation.

Every native device is read-only. Write opens, character writes and block-write
strategy requests fail with `EROFS` before transport access. Unknown controls,
including modification controls, are unsupported. Capacity and offsets use
checked 64-bit arithmetic. Reads expose a 512-byte logical view over the actual
UFS logical block size, including unaligned byte access and partial final reads;
the block-size control accepts only 512 or the actual UFS logical-block
size. Physical-media block geometry is not measured by this adapter and its
physical-size ioctl is unsupported. Completion preserves residual counts and
transport errors. The disk mutex serializes shared bounce-buffer access,
including initialization probes, and is not held across user copyout.
This adapter has no transport write API, HFS port, root-selection change or
partition-format operation.

`tests/test_ufs_disk.py` compiles the actual portable decoder/read functions and
the actual BSD strategy, open, write, ioctl and signature-probe bodies under
ASan/UBSan. Synthetic 512/4096-byte GPT fixtures cover both CRC copies, malformed
headers/tables, overflow, overlapping/out-of-range entries, I/O failure and
ambiguous labels. Read tests cover unaligned cross-block access, truncation,
EOF, canaries and partial errors. Strategy tests verify residuals, completion,
mapping failures, sector-size controls and rejection of writes before I/O.
The SYSTEM-named Android LP container case is exercised explicitly. Both new
disk translation units also compile with the current B2L BSD kernel flags.
The 0012 device photograph confirms native read-only capacity, BSD device
publication, validated primary/backup GPT and a bounded SYSTEM signature read.
The driver successfully operates after ExitBootServices. This is native storage
read proof; it does not establish an HFS mount or userspace boot.

## Native HFS root and SYSTEM writes (0013)

The 0012 UFS read milestone and verified empty SYSTEM image preparation now
support a real builtin HFS provider and native root selector. See
[HFS provider](hfs-provider.md) for the pinned source and unencrypted configuration.
Only the unique validated HFS SYSTEM view becomes writable. The whole LU and
other partitions remain read-only. Real WRITE commands and completed cache
flushes back the permissions; partial physical blocks use bounded RMW.

The loader explicitly opts into the SYSTEM development root with `rd=b2l-system`,
`b2l_root_rw=1` and `b2l_unsealed_root=1`. The native alias resolves from validated
GPT metadata; no unit-specific partition number is compiled into the selector.
No IOMedia class or Apple root-authentication success is invented.

Physical proof requires `B2L HFS: native root mounted read-write` and the first
completed native write/cache-flush line. Compilation, host policy tests and
permissions alone do not establish those outcomes. The empty filesystem has no
init, so a later missing-init panic is expected independently of mount success.

HFS's mount-time volume UUID uses the real pinned BearSSL MD5 fallback. It
marshals the legacy context explicitly, preserving incremental updates, copied
state and the existing high/low byte-counter ABI. MD5, AES regression and
actual-source HFS registration/root-policy/native-alias tests pass under
ASan/UBSan; physical mount/write proof is still separate.

The 0013 photograph now confirms both success lines, native root device selection
and explicit unsealed-root logging. HFS mounting and native write/cache flush
are physically verified. A subsequent NULL AMFI dereference in
`IOCurrentTaskHasEntitlement` is matched to the exact dSYM and callback load.
It occurs after the successful mount; empty-root path failures are separate.
