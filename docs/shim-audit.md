# B2L / A5 compatibility audit

**Verdict: CONCERNS.** Baseline: build 0027; review date: 2026-10-22.

The B2L kernel reaches real ARM64e launchd code and shared-cache libraries. Its
instruction service is separate from the A5 policy shims: successful PAC/branch
emulation does not supply AMFI entitlements, platform identity, Sandbox policy
operations or device services. Build 0027 implements only the container query
needed by this userspace; A5 has a registered permissive Sandbox policy and an
XML entitlement provider. Those policy gaps can cause permission failures, but
the latest launchd assertion does not identify its failing caller by itself.

## Scope and evidence

This is a source/ABI comparison of the port's compatibility boundaries, plus
local actual-source tests and offline analysis of the supplied boot screens.
It is not an audit of all upstream XNU, the entire firmware or every iOS service.
A5 means the prepared downstream XNU checkout, whose upstream base is
`1b191cb58250d0705d8a51287127505aa4bc0789`; its working tree contains additional
port changes and `config/MasterVersion` reports 22.6.0. B2L's pinned upstream is
`ac9718fb1af618d5ce8678d0dc6e8a58f252216f`. The different kernel interfaces and
userspace versions require adaptation, rather than copying callbacks blindly.

| Boundary | Source evidence | Assessment at 0027 |
| --- | --- | --- |
| Firmware handoff | `uefi/XnuLoader/XnuLoader.c`, `XnuHandoff.S`, `MachO.c`, `BootPayload.c` | EL1, bounded load/map checks, bounded ExitBootServices retry, cache clean and native ARM64 handoff; actual kernel startup demonstrated. |
| RAM | `uefi/XnuLoader/Arena.h:6`, `Arena.c:18-20` | Largest single conventional-memory span, 32 MiB headroom, **2 GiB cap**. Reserved holes are not merged or reused. This does not allocate 6-8 GiB. |
| CPU/timer | `xnu-overlay/iokit/Kernel/arm/B2LCPU.cpp:39-46,75,142-145` | One CPU only; real timer/self-IPI delivery demonstrated. Secondary startup and suspend are unsupported. |
| ARM64e instructions | `xnu-overlay/osfmk/arm64/b2l_compat.c`, `b2l_compat_fast.s`, `b2l_arm64e_compat.c:101-187` | Candidate state, EL0/process opt-in, faultable memory reads and executable-leaf validation. Native kernel remains ARM64. |
| Pointer ABI | `patches/0006-b2l-arm64e-compat.patch`, `0014-b2l-arm64e-dyld-page-fixups.patch` | Raw development pointers in shared-cache v3/v5 and per-image formats 1/9/12/13; no authentication security claim. |
| VM geometry | Actual prepared `vm_shared_region_pager.c`; `vm_dyld_pager.c:649-650,786-787` | Shared-cache logical 16 KiB slides on 4 KiB VM covered. Separate per-image pager requires native page geometry. |
| Signing/cache admission | `patches/0007`, `0012`, `0013` | Explicit unsigned execution/cache admission and boot identity; does not create verified cs_blobs, entitlements or platform status. |
| AMFI | `patches/0004`; A5 `bsd/kern/sweet_amfi.c:135-174,236-289` | B2L returns absent-provider results; A5 registers provider, parses XML and marks root-filesystem platform binaries. |
| Sandbox | `patches/0015`; A5 `bsd/kern/sweet_amfi.c:305-325,367-399` | B2L only missing-policy op 0x43. A5 registers policy `Sandbox`, accepts most operations and returns ENOENT for data-producing operations. |
| Privileges | B2L prepared `bsd/kern/kern_priv.c:78-125`; A5 `sweet_amfi.c:343-364` | B2L lacks A5's non-root `PRIV_WORK_INTERVAL` grant. UID 0 does not bypass every entitlement/platform check. |
| pthread/crypto | `xnu-overlay/bsd/pthread`, `scripts/stage-libpthread.py`, random/AES/MD5 adapters | Actual public provider and native adapters; host tests pass. Not evidence that all userspace pthread calls run. |
| Storage/root | `xnu-overlay/bsd/dev/b2l_ufs_{transport,disk,core}.c`, root policy | Native read/write/flush, validated GPT, writes restricted to selected HFS root extent. Journaled root mount demonstrated. |
| Console | `xnu-overlay/osfmk/arm64/b2l_early.c:61` and global cursor state | Allocation-free early/panic output works; preempting writers can interleave even on one CPU. Latest screen shows this. |
| Graphics/input | `B2LPlatformExpert.cpp`, generated device tree, overlay source lists | GOP text output is present; full IOFramebuffer, HID, GPU/Metal and IOSurface providers are not supplied by this shim. |

## Latest launchd assertion

The new screen passes `_container_init`, prints launchd's real hello/bootstrap
messages and boot arguments, then exits with namespace 7, subcode 1:
`assertion failed: 997999: launchd + 357492 ... : 0x1`.

The offset is `0x57474`. In the locally supplied executable this is the
`_os_crash` call inside a shared cold helper beginning at `0x5745c`. That helper
reads errno, calls `_os_assert_log`, then `_os_crash`. Multiple callers use it,
including coalition creation/ledger setup, Sandbox spawn attributes and several
filesystem/spawn setup failures. A particularly strong startup candidate is
`sysctlbyname("kern.hostname", ..., "localhost", 10)` at offset `0x49c5c`: its
error branch reaches `0x49eb4` and the exact shared assertion helper. In prepared
XNU, `bsd/kern/kern_sysctl.c:2918-2925` denies this write unless the process is
a platform binary or has the user-assigned-device-name entitlement. Both gates
are absent for unsigned B2L launchd at 0027. This identifies a concrete missing
contract and a matching static failure path; the runtime caller is still not
uniquely established from the photograph. The value 1 is consistent with EPERM, but is
not a unique caller identifier. The number 997999 is not enough to reconstruct
the failing startup operation. A syscall denial record or user backtrace is
needed if policy parity does not move startup forward.

The earlier preboot lookup error 25 remains present; execution proceeds past it.
The shared-region unnest warning is not itself the fatal exit. Failure to open
`/private/var/cores/launchd.core` is a later diagnostic failure (errno 2), not
proof of the initial cause. No proprietary disassembly or binaries are included
in this repository.

## Findings and priorities

| Priority | Finding / status at 0027 | Effect | Smallest action |
| --- | --- | --- | --- |
| P1 | Missing AMFI entitlement/platform provider relative to A5 | Entitlement and platform checks can deny genuine system services after successful unsigned exec. | Add opt-in development metadata/provider with native lifetime ownership; do not pretend CodeDirectory pages were verified. |
| P1 | Sandbox coverage is limited to one operation | Other checks/profile calls still receive ENOPOLICY, unlike A5's permissive policy. | Register a development policy; preserve the current iOS 26 op 0x43 output ABI. |
| P1 | Missing work-interval privilege grant | Non-root UIKit/CoreAnimation work-interval creation can fail later, as addressed by A5. | Grant only PRIV_WORK_INTERVAL under the development gate. This is not proven to cause the current PID 1 assertion. |
| P1 | PID 1 assertion caller remains unresolved | Cannot certify launchd service startup or diagnose a single syscall from the photo. | Check runtime after policy integration; if unchanged, capture the specific failing call/backtrace. |
| P2 | 2 GiB arena and single CPU | Earlier request for 6-8 GiB is not implemented; startup performance remains limited. | Plan discontiguous RAM/SMP separately with reserved regions preserved. |
| P2 | Trap cost and no translation cache | A compatibility instruction can require exception handling, two executable-leaf VM queries and multiple scalar instruction reads. | Profile actual boot before introducing a versioned translation cache. No speed claim follows from assembly alone. |
| P2 | Console interleaving and unavailable core directory | Failure context can be overwritten or split. | Preserve an ordered log/backtrace through an initialized sink; retain allocation-free early panic output. |
| P2 | Per-image 16 KiB geometry / signed 16 KiB CodeDirectory support absent | Other binaries can fail outside current tested geometry; relaxing guards alone is unsafe. | Extend each path with bounded mixed-page tests when an actual image needs it. |

Missing GPU/HID/display providers are accepted bring-up scope gaps, not a
regression fixed by AMFI/Sandbox parity. An A5 Exynos 7580 GPU/display driver
cannot serve as an Exynos 9820 device driver unchanged.

## Verification performed

All checks below passed against the 0027 source used for this comparison:

- `scripts/test-loader.sh`: Mach-O boundaries/entry, DT ABI/topology/seed,
  conventional-memory arena exclusions and framebuffer progress bounds.
- `scripts/test-arm64e.sh`: 115 LLVM encodings; 200,000 C/assembly differential
  cases per variant; actual wrapper and grading boundaries; shared-cache
  mixed-page pager scenarios with a negative regression control; QARMA5 host
  vectors and Cortex-A53 QEMU assembly execution.
- `scripts/test-ufs-transport.sh`: read/write/flush descriptor bounds, status,
  deadlines/DMA behavior and malformed EFI device paths.
- Sixteen provider/patch checks: unsigned cache, unsigned boot identity, dyld
  fixups, container query, unavailable AMFI/trustcache, real libpthread adapter,
  random services, three console checks, UFS disk, HFS, patch sequence, AES, MD5.

These are behavioral harnesses around actual source with mocked kernel/VM
boundaries. They do not replace physical scheduling, DMA coherency, endurance,
concurrent service startup or GUI testing. No new device write was needed for
the audit tests.

## Coverage assessment

| Dimension | Result | Boundary |
| --- | --- | --- |
| Correctness | CONCERNS | Covered contracts pass; full launchd startup fails at an unresolved assertion. |
| Edge cases | PASS within harness scope | Malformed sizes, copy faults, register rollback, chain/page bounds and policy precedence exercised. |
| Errors | CONCERNS | Most adapters propagate faults and UFS deadlines are bounded; exact PID 1 failing caller is missing. |
| Security | CONCERNS by accepted PoC design | Unsigned execution, identity PAC/AUT and unsealed writable root are explicit development choices; no production authentication/isolation claim. |
| Concurrency | CONCERNS | UFS and key initialization use initialized mutexes; early console remains unsequenced. No SMP stress proof. |
| Performance | CONCERNS | Static trap/VM-lookup and one-CPU costs; no device profile or translation cache. UFS flushes every successful write. |
| Tests | PASS within scope | Source-level tests, sanitizer checks and QEMU assembly pass; kernel/user runtime remains a separate gate. |
| Documentation | CONCERNS | Build notes document narrow contracts; “full shim parity” would overstate 0027. |

## Integration direction

The user requested A5-style policy parity following this audit. Preserve B2L's
working ARM64e/PAN/pager service, add registered Sandbox and work-interval
behavior, and adapt AMFI entitlement/platform metadata to current XNU APIs.
A5's ENOENT behavior for operation 0x43 must not replace the verified iOS 26
zero-output success contract. XML-only entitlement support remains narrower
than full DER/CoreEntitlements. A successful local build must still be followed
by a new physical launchd result before claiming the assertion is fixed.
