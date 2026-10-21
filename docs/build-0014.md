# Build 0014: unavailable AMFI entitlement handling

## Device evidence

Build 0013 physically mounted the native HFS SYSTEM root read/write and
completed a native UFS write plus cache flush. It printed the explicit unsealed
development-root policy, then continued into VM/swap startup. The subsequent
panic was matched to the exact kernel/dSYM: `IOCurrentTaskHasEntitlement`
dereferenced the absent AMFI interface while looking up its boolean query
callback. The photographed FAR of 0x1c0 matches the callback offset.

## Policy

Unavailable entitlement queries must deny or report unavailable, without
inventing an AMFI provider, signed status or entitlements. Existing successful
provider behavior remains separate from this absent-provider handling.
This update does not supply iOS userspace. The intentionally empty HFS root
still has no init and lacks its normal `/dev` and swap mount-point directories.
Those missing-path errors are separate from a successful root mount.

## Storage boundary

The HFS and UFS implementation is unchanged from 0013. SYSTEM is not rewritten,
and original Android BOOT/SYSTEM backups are retained without new backups.
The reference physical RAM/MMIO guards remain unchanged.

## Host verification

The kernel/dSYM build and BOOT packaging passed. Actual-source ASan/UBSan tests
cover absent, stale and partial AMFI interfaces; boolean/string/integer/object
queries; vnode and user-client bridges; cleared failure outputs; malformed
success with no object; provider success paths and object release counts; and
unavailable/legacy/real context adjustment callbacks. Independent review found
no remaining blocker. The real successful-provider path is retained.

Only the unavailable AMFI patch is added to the prior HFS/UFS kernel. The native
root, read/write support and loader boot arguments are unchanged. Physical
continuation past the earlier entitlement fault remains a device test.

The kernel is 18,479,408 bytes and the validated BOOT image is
23,052,912 bytes. Exact symbols remain local under ignored `out/0014/symbols`.

## Authorized installation

The BOOT-only update completed with target checks and exact image-prefix
readback verification, then reboot was requested. SYSTEM was not rewritten.
Original Android backups remain intact; no additional backups were taken.
Physical continuation past the photographed AMFI fault is pending device evidence.

## Device result and filesystem correction

The next device photograph stops before the entitlement path: HFS refuses the
dirty non-journaled root left by the earlier 0013 panic. This does not establish
whether the AMFI fix has passed on the phone. The filesystem was subsequently
[repaired and journaling enabled](system-journal.md), then the unchanged 0014
BOOT was retried. Physical continuation remains a separate device test.

After the journal correction, the device photograph confirms a successful native
read/write HFS root mount, continuation beyond the previous AMFI callback fault,
and an actual attempt to load `/sbin/launchd`. The final panic is ENOENT (errno 2),
matching the deliberately absent init file. The XNU kernel/native-storage/root
mount milestone is achieved; no iOS userspace execution is claimed.
