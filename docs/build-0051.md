# Build 0051: explicitly approved signature-presence status compatibility

## Actual IPSW expectation

The local iPhone17,2 iOS 26.6.2 / 23G90 restore image's shared-cache code
provides a concrete explanation for the current container-client rejection:

- libsystem_containermanager's status helper at unslid 0x1e2fdc3b4 calls
  csops_audittoken operation 0. It maps raw CS_SIGNED bit 29 to MCM status bit 0.
- ContainerManagerCommon's clientIdentityWithClient:proximateClient:error:
  at 0x1e3014184 calls container_client_is_signed at 0x1e3014218.
  The next instruction branches to an error path at 0x1e3014344 when false.
- Only after this gate does it request the client's CDHash (0x1e3014234),
  identifier and entitlements. Thus original CDHash metadata alone cannot
  satisfy that earlier gate.
- XNU defines CS_SIGNED as signature presence, including signatures that may
  have become invalid. The previous development status reply added platform
  and validity compatibility flags but omitted this presence bit.

These addresses come from local binary inspection, not a runtime trace of the
new image. The gate mismatch is established; it is not yet proof that resolving
it eliminates every container crash. lsd, ManagedSettingsAgent, AppProtection
and mobileactivationd all previously failed while obtaining container paths.
passd and cryptexd require separate verification.

## Approved scope and implementation

Automatic approval review initially rejected a status change without explicit
approval of its precise security scope. The user then explicitly approved:
only active B2L development mode, platform-classified targets and existing
original CodeDirectory/CDHash metadata may add CS_SIGNED to CS_OPS_STATUS.

Patch 0036 implements exactly this query compatibility. Metadata is read after
releasing proc_lock, while the original dispatch still holds its process
reference. Native target lookup, audit-token validation and copyout remain.
Metadata errors leave the bit absent. Existing native CS_SIGNED and other flags
retain their native precedence. No internal process flag is changed, no cs_blob
is created, and no CMS, page-hash validation or Apple trust chain is supplied.
The previous development CS_VALID query compatibility remains as before.

At most twelve status queries made by containermanagerd produce B2L CSSTATUS
records with caller, target, reply flags and metadata result. This should show
whether actual MCM queries receive the new bit on the next capture.

## Checks

- Actual prepared CS_OPS_STATUS case passes ASan/UBSan: active development,
  platform and metadata gates; absent/malformed/unsupported metadata and I/O
  or allocation errors; native flags; enforcement, forced-LV and platform-path
  precedence; unchanged internal flags; unlocked metadata; copyout faults;
  null output address; and bounded logging.
- Existing actual unsigned identity dispatch test passes, including native
  signed-target precedence, target ownership, sizing and copy/metadata faults.
- Full 36-patch preparation passes upgrade from 35, repeated idempotence and
  refusal of a local source modification.
- Kernel build succeeds. EFI and loader are byte-identical to 0050.
- Kernel, unstripped kernel and dSYM UUID:
  3D6160E4-2B42-32CD-9F55-DB4100097A02.
- BOOT: 23,190,552 bytes; SHA256
  a665256cd7c653a14f73c848b9c2fdd0a73b0116044b427a8dbff1f9172534a7.
- Kernel SHA256:
  37510cc9047c7c9db566d20de892646b6c29c04f71b1caf37647819d092016b3.

## Device installation

Confirmed recovery, root, beyond2lte identity, exact BOOT device and capacity,
and current complete 0050 BOOT prefix hash. Wrote only BOOT, then matched the
entire 0051 image prefix hash. USERDATA's 4096-byte header was unchanged.
SYSTEM and USERDATA were not written from recovery. The existing V2 cache and
four workers are retained. Reboot was requested successfully. Retained 0050
BOOT is the matching rollback. New userspace stability awaits runtime evidence.

## Device evidence after installation

Read-only recovery capture of epoch 17 has 103 valid framed records,
212,816 decoded bytes, zero missing positions and zero producer drops.
Raw page CRCs validate, but concurrent console writers interleave portions of
some lines before framing; those fragments cannot be reconstructed reliably.
A separate current launchd.log (708,511 bytes) was extracted from the HFS catalog
without mounting, replaying or writing USERDATA. A dirty catalog snapshot may
omit newer journaled changes.

### Status query and remaining container failures

The trace contains complete CSSTATUS samples for caller/target 101 with
flags=0x24000001 and signature_metadata=0; a sample for caller/target 72 is
interleaved. This proves the new presence reply executes for the MCM daemons'
self queries. It does not prove that they successfully queried each failing
client: no complete cross-process sample was captured.

The current launchd file records these repeat exits:

| Service | Exits | Mechanism |
| --- | ---: | --- |
| lsd | 18 | SIGABRT |
| appprotectiond | 15 | SIGTRAP |
| ManagedSettingsAgent | 14 | SIGTRAP |
| mobileactivationd | 15 | SIGTRAP |
| passd | 4 | SIGTRAP |
| cryptexd | 13 | SIGTRAP |

Shared-cache slide is 0x16c6c000. Backtraces still map to CoreServices' same
container abort (lsd), AppProtection's same main-container assertion,
ManagedSettings' same system-location assertion, and FinanceKit's
swift_unexpectedError path (passd). The unsigned status mismatch was real, but
0051 has not resolved these crashes. Their remaining MCM error needs direct
client-request/error evidence, rather than assuming another signature bit.

containermanagerd (72) and containermanagerd_system (101) start successfully;
no exit of either is present in this file. SpringBoard (28), backboardd (62)
and runningboardd (27) are spawned; no SpringBoard exit is captured. Repeated
AXSpringBoardServer lookups still fail. Spawn/running state does not establish
UI readiness.

### New concrete Image4 expectation

cryptexd no longer stops at the old product-node message. Its current message
is `panic: non-sensical crypto hash method: `, with a blank value. Symbolization
places its caller at libimage4 0x2a4471874 (return-address adjustment applied),
inside personalized-AP chip selection. Actual IPSW instructions at
0x2a44716b4..0x2a4471710 request `crypto-hash-method`, then compare the returned
bytes against `sha1` or `sha2-384`, otherwise taking this panic branch.
The generated board tree currently supplies neither property. This is a
specific missing platform-description input, not evidence that corecrypto's
cryptographic implementation failed. Choosing an appropriate value still
requires checking the actual consumers and supported development path; merely
adding the property is not proof of Apple personalized-image support.

Other repeat exits include fairplayd.H2 exit(190), PerfPowerServices exit(1),
and webinspectord exit(1). Their launchd exit records do not contain enough
information to assign a root cause. The camera CPU-monitor entitlement denial,
unsupported rename-swap and key/encryption errors are also still present.

### CPU and native cache

- Six CPUs again pass per-CPU timer and bidirectional SGI1 verification.
- Four cache workers reach peak four; CPU mask 0x3f and all worker counters
  advance. There is no kernel panic in the captured console.
- Startup sees 21,391 durable cells. Pre-init applies 5,439; 15,952 already
  match; none are skipped. FIPS_POST returns true.
- Last complete sample: queued=32,522, learned=14,519, skipped=17,179,
  dropped=37,909, errors=0, pending=0, active=0. Flush errors remain zero.
  learned counts updates, not distinct cells; dropped is a learning-queue
  statistic, not failed disk writes or lost console bytes.
- Both live disk-header CRCs validate. Latest generation 17,230 commits
  22,690 cells of 241,271 slots, 1,299 more distinct cells than startup.

Raw traces, catalog/file extracts, live headers and symbolization remain local
under ignored out/logs/0051-* paths. This inspection performs no device writes,
installs no new image and leaves the device in recovery.
