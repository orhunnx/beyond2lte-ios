# Build 0098: preserve launchd's cache handoff length

## Confirmed cause

0097 exits with `No service cache`. Disassembly shows launchd allocates shared
memory from its original 2,279,863-byte launch cache and later parses that same
logical length. The native cache-loader child copies only its input file's
length and does not update the parent's logical length.

Our 0096/0097 derived cache was valid by itself but only 1,198,735 bytes. Copying
that prefix left the original file's tail in the shared view. The plist trailer
therefore described stale data when launchd parsed the original full length.
Standalone parsing and a successful child copy had missed this contract.

## Correction

The generator now requires the derived binary plist to fit the original logical
length. It inserts zero padding before the offset table, preserving all object
bytes and offsets, moves the table and trailer to the end, and updates the
trailer offset-table location. The result is exactly 2,279,863 bytes. If the
cache cannot fit, generation fails rather than handing off a mismatched size.

The native Mac XPC parser accepts the padded cache with 691 jobs. Removing the
single owned bootstrap entry leaves an object equal to the original 690-job
cache. Original and derived lengths match; generated derived SHA256:
`5dfa23e46e09cc784293bd3e40517a643d5328eef3b34d24e68fbbac998c5098`.

The separate published path becomes
`/private/var/db/b2l-launchd-bootstrap-v2.plist`. The exclusive publisher and
exact cache-loader argument selection both use that path. Existing v1 files
are retained but no longer selected. The owned executable, job, migration
entitlements and one-attempt behavior remain the same as 0096. Source-image
cache bytes stay in ignored private build outputs.

## Validation

The handoff test reproduces the old short-copy corruption against a dirty
original-size buffer, then verifies the corrected full-size replacement parses
the expected object. It checks exact length/trailer placement, unchanged-size
identity, malformed tables and oversize rejection. The native argument-copy
regression test passed. The production XNU kernel compiled; scalar code-generation
checks passed and RCpc audit found zero unsupported instructions.

The native Mac XPC parser and equality comparison also passed using the actual
generated padded bytes packaged into this kernel. Firmware and loader hashes
match 0095. These checks establish the build and handoff model; device runtime
acceptance and native application migration still require a fresh boot capture.

## Installation

Installed to the S10+ BOOT partition from root recovery after product, alias and
57,671,680-byte capacity checks. A full BOOT backup was taken and its prefix
matches the previously installed 0097 image. The 25,895,984-byte image was
written and read back exactly; SHA256:
`1d8b5074e881ee1cda6ff171a1154b25de41fb745a0648f5d912133391c6af36`.
Reboot returned successfully. Private symbols, payload metadata, backup and
transaction receipt remain under `out/0098`. Fresh device runtime is pending.

## Subsequent runtime

The 0098 capture confirms launchd starts services and spawns the owned job, and
native MobileGestalt regenerates the expected iPhone artwork profile. The
helper crashes in arm64e dyld before main, and opening Settings produces the
IOGMD wire-count panic. See [runtime findings](0098-userspace-findings.md) and
[0099](build-0099.md).
