# B2L ARM64e development runtime

This is an opt-in runtime compatibility service for the plain ARM64 B2L kernel.
It leaves `HAS_APPLE_PAC` disabled and retains the real ARM64_V8 host subtype.
It does not modify executable files or claim authenticated-pointer security.
[Build 0028](build-0028.md) separately supplies explicitly unauthenticated
AMFI development metadata, without a verified signature or fake crypto provider.

## Activation and execution

`b2l_arm64e_compat=1` is parsed at kernel TUNABLES startup. Its message uses
the lock-free early framebuffer sink because normal printf/log locks are not
yet initialized at that startup phase. Other values leave the
service disabled. Binary grading accepts versioned ARM64e ABI version 0 only
when this service is enabled. Native ARM64_ALL/V8 grading is unchanged. The
process records its actual executable ARM64e subtype; the exception wrapper
checks that process metadata and EL0 AArch64 state before invoking the decoder.
Kernel exceptions and ordinary ARM64 processes never enter the compatibility
service.

The undefined-instruction hook reads the trapped instruction through XNU's
existing fault-aware COPYIN path. The wrapper snapshots x0-x28, FP, LR, SP, PC,
and CPSR. The decoder produces a separate candidate; state is committed only
on successful emulation. Unhandled instructions and callback faults retain the
original exception path and saved state. Next PCs require an aligned, canonical
nonzero EL0 address, VM execute permission, and successful instruction copyin.
No pointer address bits are guessed, masked, or canonicalized.

The native scalar assembly decoder computes PAC/AUT/XPAC and authenticated
branch/return candidates. C performs bounded memory callbacks and validates all
next PCs before committing. Authenticated loads use a raw base followed by the
architectural offset and load. LDAPR uses fault-recoverable aligned XNU atomic
copyin32/64 loads and a DMB ISH acquire barrier; byte/halfword reads extract the
requested scalar from its containing aligned 32-bit user word. No helper uses
x18, SIMD/FP, Apple system registers, or hardware PAC instructions.

## Pointer ABI and software PACGA

PAC/AUT/XPAC operate as identity transformations in the explicitly selected
**stripped-pointer development ABI**. They do not validate signatures. Therefore
all pointer-producing fixups must provide unsigned addresses consistently.
Shared-cache v3/v5 authenticated fixups have a B2L opt-in raw-target writeback
path. Native hardware-PAC paths are unchanged. Since these raw pointers are the
same across tasks, no per-task authenticated-cache key remapping is introduced.
The kernel supplies dyld's `ptrauth_disabled=1` apple string for opted-in ARM64e
images. This handoff does not alone establish compatibility with every dyld or
per-image chained-fixup implementation.

PACGA is a software QARMA5 hash computed by the scalar AArch64 helper, not an
identity or constant result. A 128-bit key is generated lazily from XNU's
boot-seeded PRNG under an initialized mutex on the first EL0 PACGA operation.
The key is shared across processes for the lifetime of one boot; it is neither
persisted nor logged. Per-process PAC key changes are not emulated. Portable C
is retained as an independently tested reference implementation.

## Integration and verification

`0006-b2l-arm64e-compat.patch` supplies grading, the exception hook, raw cache
fixups, dyld handoff, source lists, private header export and per-object build
flags. New sources are a separate `xnu-overlay/osfmk/arm64` overlay. The core C
object requires `B2L_COMPAT_USE_ASM`; all C helpers use `-mgeneral-regs-only`.
The native wrapper, core, QARMA, four modified runtime translation units, and
two assembly helpers passed ARM64 iOS 26.5 object compilation.

Focused tests compile the actual shipped wrapper/core/QARMA sources under
ASan/UBSan with mock fault, execute-protection, entropy and lock boundaries.
They verify opt-in rejection, process/EL restrictions, failure without register
mutation, FP/LR/SP/CPSR preservation, branch validation, one-time random-key
initialization, stable PACGA output and acquire-load width/alignment behavior.
A separate harness compiles the actual grading and process-classification
functions to verify the ABI/version gate and unchanged host identity.

## Explicit remaining requirements

The B2L pager assembles a 16 KiB logical slide page from four separately
faulted 4 KiB source pages in wired scratch memory. It validates the original
chain offsets, applies the raw v3/v5 rebases there and copies only the requested
4 KiB slice into the destination UPL. Backing files stay unchanged. Busy pages,
object locks and paging references are released between source faults. Each
fault attempt gets a fresh fault-info snapshot because shadow traversal can
adjust its offsets. Partial logical pages and malformed chains fail closed.
The scope is enabled B2L compatibility with 4 KiB VM / 16 KiB slide pages only.
Actual-source ASan/UBSan tests cover noncontiguous physical pages, all slices,
malformed chains, flags, transactional faults and VM-reference cleanup, with a
negative control that reproduces the earlier mutable-fault-info bug.

This slide-page support does not implement 16 KiB code-signature validation on
a 4 KiB kernel. The existing CodeDirectory admission guard remains unchanged;
simply relaxing it would risk hashing beyond a physical page. That is a
separate signed-execution integration requirement.

The unavailable-AMFI guards remain in place. Build 0028 provides unsigned
rootfs XML entitlement/platform metadata; signed-executable context adjustment
and DER/CoreEntitlements remain unsupported. Other unimplemented ARM instruction extensions,
userspace ABI expectations, page-size assumptions, dyld per-image fixups,
code-signing formats and unavailable device/framework services remain possible
subsequent blockers. Passing host/native compilation does not establish that
launchd or SpringBoard runs on the device. Trap emulation also carries per-trap
exception/copyin/VM lookup overhead; it is not a native-speed execution claim.

## Reproducible checks

Run on an AArch64 Mac with Xcode command-line tools, LLVM tools, Python 3 and
QEMU installed:

```sh
bash scripts/test-arm64e.sh /path/to/prepared/xnu
python3 tests/test_userdata_patch_sequence.py /path/to/pinned/xnu
```

The test runner writes generated fixtures and binaries only into a temporary
directory. The code and all committed fixtures are project source, public
algorithm parameters or synthetic data; no Apple binary or device key is included.

## PoC scope and later cache

Build 0016 includes this resident kernel service and enables its boot argument.
This is a proof of concept, not complete iOS compatibility. Every unsupported
instruction currently enters the exception path; the assembly handlers and
QARMA avoid depending on native PAC, but do not remove exception overhead.
There is no background daemon, on-disk executable rewriting or translation
cache. A separate versioned RAM/persistent translation cache is the next phase.
First device evidence should distinguish architecture admission, actual EL0
execution and subsequent failures. A successful build alone proves none of
those runtime stages.
