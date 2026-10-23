# Build 0063: file extension bookkeeping and existence-only LWCR predicates

## Evidence and changes

Read-only recovery of the SYSTEM log region retained epochs 22, 28 and 29.
The normal decoder selected only epoch 29; older valid pages were separately
recovered by epoch and CRC. Read-only HFS catalog/data-fork extraction also
recovered 58 crash reports without mounting or replaying USERDATA.

AppPlaceholderSync's SelfDestruct report explicitly states "Sandbox token not
sent from container". Development Sandbox operation 5 previously returned
ENOENT. Operations 5/6/7 now implement private B2L file capabilities with
random tokens, path/class bookkeeping, optional PID/version binding, owned
consumer handles, reference counts, copyout rollback and process-exit cleanup.
Only file read/read-write classes are supported; unknown classes/types and
release-last operation 0x50 explicitly remain unsupported. These capabilities
record existing unconfined development behavior; they neither authenticate an
Apple sandbox token nor implement production VFS sandbox enforcement. The
existing unsigned-development and 64-bit-process gates remain required.

New familycircled reports show SwiftXPC's internal crash after construction of
an entitlement-existence peer requirement returns nil. This differs from the
older libxpc match/error-code inconsistency already corrected in 0060.
Native iOS 26 libxpc accepts both match code 0 and genuine nonmatch code 1 when
constructing a requirement, but rejects unsupported code 3. Operation 0x66 now
strictly recognizes only the observed version/category/compiler and single
entitlement-existence DER schema. It evaluates key presence in the existing
credential metadata of the native audit-token target. Unknown predicates stay
ENOTSUP/code 3; malformed input and copy/allocation failures receive initialized
failure results. No new entitlements, signing flags or signature trust are
created. Native audit-token lookup and a subsequent PID-version check constrain
process identity and exec races.

Assetsd throws "assetsd requires TCCPhotosService access" during native Photos
preflight. Its original metadata already contains the Photos allow-array; the
actual runtime denial is not yet established. Bounded opt-in assetsd/tccd
csops result/header and AMFI error records, plus original entitlement metadata
presence, were added. They do not change authorization or syscall results.
The bounded crash backtrace whitelist now includes the three service names,
including appplaceholdersyncd's truncated p_comm.

## Verification and installation

Actual extension/dispatch and LWCR callback host tests pass ASan/UBSan,
including copy faults, reference lifetime, forged/stale/bound tokens, concurrent
publication/release, malformed/unknown DER and native-policy precedence.
TCC diagnostic and metadata lifecycle tests pass their gates, copy errors and
logging limits. The real DEVELOPMENT ARM64 B2L kernel builds successfully.

The verified 0062 Android firmware/loader prefix was reused byte for byte.
0063 is 23,372,608 bytes, SHA-256
0840e413f2fca6fb98c78884ed6c6049150651ded8358e73f207a04298729605.

The connected beyond2lte recovery and BOOT capacity were checked. Existing BOOT
was backed up, its image prefix matched 0062, and only BOOT was written. Full
0063 image-prefix readback matched SHA-256 and reboot was requested. SYSTEM,
USERDATA and RECOVERY were not written during this installation. Private
images, crash reports, readbacks and receipts remain under ignored out/.
Runtime exception removal and UI progress require the next device capture;
assetsd is an open diagnosis, not a claimed fix.

Build dependencies and the environment file are now retained in
out/build-env-0063 rather than an OS temporary directory. The matching kernel
and debug symbols are retained under out/0063. No downloaded Apple dependency
or private binary is committed.
