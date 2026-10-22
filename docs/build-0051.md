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
