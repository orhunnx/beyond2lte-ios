# Build 0056: Mach service provenance and applied-cache reclamation

## Development Mach message-filter provider

The existing explicit B2L development Sandbox initialization now registers all
version 1 Mach message-filter callbacks before machine lockdown and user service
creation. Each native service port receives a unique, refcounted identity.
Native connection derivation transfers a retained parent identity to its
connection label. Connection validation accepts only the same non-null identity,
releases both input references, and rejects missing or mismatched labels.
Native port rights, types, derivation and bootstrap domain restrictions remain
in effect. No signing or entitlement behavior is changed.

The provider uses the existing unconfined development gate. It is not production
Sandbox enforcement. A preexisting provider is not overwritten. Registration
status appears as `B2L Sandbox: Mach filter v1 provider registered (0)`.

## SYSTEM cache reuse

Applied source pages are synced/read back before their original/actual-native
records are appended and committed to the root-only USERDATA undo archive.
Only then are both SYSTEM copies erased and their cells reused. A/B scan-range
headers are trimmed where possible. Archive headers, record hashes and readback
protect publication; record length alone is not used as a validity check.
See [native-cache.md](native-cache.md) for the exact ordering, maintenance boot
requirements, failure behavior and new counters.

The archive consumes USERDATA space. Older maintenance images cannot restore
originals moved there; use 0056-or-later with `b2l_cache_restore=1`.
The firmware/loader, memory reservations, six-CPU topology and four runtime
cache workers are unchanged.

## Verification

- Actual provider callbacks pass ASan/UBSan ownership, matching/mismatched label,
  missing input, allocation failure, development-gate and registration tests,
  including concurrent reference ownership cycles.
- Existing Sandbox dispatch/privilege tests pass.
- Native cache tests pass format, original/native validation, partial writes,
  index collision/slot reuse, undo publication and reverse-order restoration.
- Cross-store fault injection covers source writes, undo record/header writes,
  both SYSTEM erasures and scan-range trimming: a modified page always retains
  a published original and can be restored.

A completed build and verified BOOT readback establish installation only.
Container service acceptance and successful GUI startup require the next device
runtime capture. Private device logs and installation receipts remain local.
