# Build 0068: observe rejected native XPC connection provenance

## Finding and remaining attribution

0067 native logs were acquired read-only from recovery: 549 files, 773,995,148 bytes, including 98 tracev3 and 78 historical crash reports. The console region is epoch35, 4,144,005 bytes from sequence0 with no producer loss. Native current-boot Persist/e8..eb contain 341,737 failed SpringBoard check-in verification messages and the same number of bogus check-ins; backboardd repeats backlight reconnection/failed observation 51,603 times.

The rejected SpringBoard listener is com.apple.frontboard.systemappservices. Shipped libxpc calls mach_port_is_connection_for_service (MIG3240, reply3340), rejects kr4, and deliberately uses MPO_ANONYMOUS_SERVICE=0xfffffffe for endpoint-created forward channels. Pinned native XNU skips service-label derivation for that sentinel. B2L currently requires two identical non-NULL service/connection labels; it can reject a valid anonymous endpoint. This is a static contract-gap candidate. Runtime attribution still requires matching the constructed object to the rejecting validation; port names cannot be compared across task spaces.

Native disassembly and focused source evidence remain local in out/research-0068/libxpc-checkin.md and .txt. Full decoded logs and per-source samples remain out/logs/0067-recovery. Mandiant v0.7.0 decoded the raw datastore; Apple log rejected it, missing-UUID warnings remain, and custom-platform timestamps are not yet calibrated. Historical crashes must be separated by boot UUID.

## Diagnostics only

Patch0045 records successful native Mach port construction after the port lock is dropped: task-local name, requested flags/parent, actual type and obfuscated port/label keys. Validation records snapshot native types and keys while owned, then emit after unlock and after callback reference consumption, without dereferencing released labels. Stage0=input/provider gate, 1=service lookup, 2=service type, 3=unlabeled service success, 4=connection lookup, 5=provider callback.

Only exact SpringBoard/backboardd, 64-bit, b2l_diag=1 processes emit these records. Each role has separate saturating quotas of64 construct and32 validate observations. Diagnostics do not allocate or log under native port locks and do not change results, rights, message contents, filter policy, signing or entitlements. The existing strict provenance provider is unchanged.

The decoder retains records in mach_provenance_records. To test the anonymous candidate, join constructor port key to validation connection_port key, expecting a forward flags0x283f/parent0xfffffffe/type connection/label0 construction followed by service-only labels and result4 at stage5. A different observed type/parent/relation must redirect the investigation; do not implement blanket NULL-label success.

## Verification and installation

- Actual diagnostic C code passes ASan/UBSan tests covering role/opt-in gates, formatting, separate quotas, and counters seeded near UINT_MAX.
- The actual patched native validation function compiles in a lock/lifetime harness; all native return stages, retained-key snapshots and unchanged return/filter values pass.
- Existing provider ownership/relation tests pass; no provider behavior changed.
- Real DEVELOPMENT ARM64 B2L kernel builds; both new marker strings are present in the packaged Mach-O.
- BOOT is23,373,672 bytes. SHA256 9e1a69582e064f20077d2e59186965093bd38ea40b2014a46a29e98783bcac68.
- Firmware/loader prefix matches0067 byte for byte. Full previous BOOT backup and all0067 logs were saved first.
- Target serialR58M33609TR, rooted beyond2lte recovery, BOOT capacity57,671,680 were checked. Only BOOT was written. Readback matched the exact image hash and reboot was requested.

This verifies installation, not the anonymous-port hypothesis or a GUI fix. The next short runtime capture is required.
