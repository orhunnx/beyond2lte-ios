# Build 0025: boot identity for unsigned userspace

## Runtime evidence

Build 0024 passed the prior dyld authenticated-branch boundary and executed shared-cache code. The new device exception is an intentional `BRK #1`, not an unsupported instruction. Matching the PC and LR with a common cache slide identifies libsystem_malloc: the return address follows a direct call to the nearby fatal stub, whose diagnostic is `BUG IN LIBMALLOC: couldn't find executable_boothash`.

XNU normally emits `executable_boothash` only after retrieving a CodeDirectory hash. The development unsigned loader intentionally has no attached cs_blob, so this metadata was omitted. The existing preboot lookup error is a separate unresolved issue.

## Correction

Patch 0013 adds a B2L-only fallback when no CDHash is available and global process signing enforcement is disabled. It derives a domain-separated SHA-256 value from the existing boot salt, boot-session UUID, filesystem identity, file ID and architecture offset, and emits the standard 40-hex-character executable boot identity. The value is stable for that file identity within a boot and varies across boot identity inputs. It does not hash or authenticate executable content.

No `executable_cdhash`, cs_blob, verified signature, platform status or entitlements are fabricated. The original signed-executable path is unchanged. Missing file attributes and user-string insertion failures propagate as exec errors. The apple-vector count advances only after successful insertion. No guest library or shared-cache file is patched.

## Verification

The actual helper and fallback dispatch branch pass ASan/UBSan host tests for output format, deterministic same-identity output, separate filesystem/file/architecture/boot inputs, signed and enforcement-enabled gates, missing attributes and insertion failure. Native XNU compilation and package checks pass; exact stripped/unstripped/dSYM UUIDs are preserved in ignored local output. The thirteen-patch sequence is checked for fresh application, upgrade, idempotence and local-edit refusal.

Only BOOT is installed. USERDATA, SYSTEM, GPT and recovery are unchanged. Full launchd startup remains unverified until the next device boot. Inspection-only cache excerpts, identifiers, binaries and installation receipts remain outside Git.
