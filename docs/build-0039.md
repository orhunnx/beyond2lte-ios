# Build 0039: original DER entitlement metadata queries

## Evidence and scope

The recovered 0038 epoch-11 log contains 939 valid records and 882897 text
bytes with no missing positions or producer drops. FIPS passes. The previous
SpringBoard and watchdog failures are not observed. Persona and user-session
files are now present and parse as property lists; the persona manifest has
version 1 and one user. The bootstrap helper itself reports EINVAL, so its full
publication sequence is not claimed verified. Native usermanagerd no longer
shows its prior manifest abort in this capture.

ManagedSettings reaches a Swift fatal error in SystemLocations when its shared
system-group container path is unavailable. Its caller uses the real
container_system_group_path_for_identifier API. The SystemGroup directory is
present but no committed child files appear in the read-only catalog snapshot.
The dirty root is not mounted/replayed in recovery, limiting negative inventory
claims. lsd has 22 observed crash reports; its complete cause is not yet known.
Its executable delegates to LaunchServices and carries system-group/container
entitlements that its clients need to query.

Local Security framework disassembly establishes that SecTask loads entitlement
metadata using csops operation 0x10 (CS_OPS_DER_ENTITLEMENTS_BLOB), first querying
the GenericBlob header and then copying the full blob. It does not route this
through the custom XML query. The existing B2L adapter supplies XML metadata
but leaves the DER operation on native code paths requiring a validated cs_blob,
which this explicitly unsigned boot does not create. This is a confirmed
interface gap. It is not yet established as the complete cause of both crashes.

## Change

The bounded Mach-O metadata reader now locates original signature slot 7 and
copies its opaque DER GenericBlob, with the existing signature/slice/overlap
checks, correct magic and a 64 KiB limit. Duplicate or malformed slots fail.
No DER entitlement is generated, widened, or treated as an authenticated input.
Userspace remains responsible for decoding and validating the DER contents.

Each unpublished credential metadata object receives an owned immutable copy
of the original DER bytes. Fork/credential references preserve its lifetime;
query copies outlive their source object and are freed independently. The new
CS_OPS_DER_ENTITLEMENTS_BLOB adapter uses the existing target and audit-token
access checks and the existing csops_copy_token sizing protocol. It is gated
by the existing unsigned development provider; no signed VM object, cs_blob,
trust-cache entry or opaque CoreEntitlements context is fabricated.

Bounded crash tracing also includes lsd, four samples per class, so a remaining
abort can be attributed on the next run. Existing CPU/RAM reservations, native
cache behavior, FIPS exclusion and persona bootstrap are retained.

## Verification and installation

ASan/UBSan tests pass for opaque DER reads, truncated output capacity, absent,
malformed and duplicate slots, I/O failures, 20k synthetic mutations, retained
copies, allocation failures and credential replacement/lifetime handling. The
existing XML, CodeDirectory identifier and 20k mutation tests also pass.
The real 26-to-27 patch sequence passes with idempotence and local-edit refusal.
The DEVELOPMENT kernel build and existing native-cache/emulator scalar checks
pass. Matching kernel/unstripped/dSYM UUIDs are retained privately.

BOOT size: 23202960 bytes. SHA-256:
`717df5abde472b9e63ea9312679f00340e8b863c279621c8442552e5a9227d13`.
The installed 0038 image prefix was verified before writing. Complete 0039
prefix readback matches after writing; reboot was requested. Only BOOT was
updated in recovery. The embedded EFI loader was verified during packaging.

Successful container lookup, lsd stability and graphical startup still require
the next device capture. Raw logs, Apple files and private metadata remain
ignored local artifacts.
