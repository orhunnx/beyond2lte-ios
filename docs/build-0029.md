# Build 0029: unsigned CodeDirectory identifier metadata

The 0028b device passes policy registration, platform status and launchd's
initialization, then stops during the mount-phase-1 boot task because
CS_OPS_IDENTITY returns EINVAL for the unsigned mount process. This is an
identifier query, not a newly enabled signature-verification operation.

Patch 0019 preserves existing csops access checks and signed/disabled-provider
paths. For an unsigned process in the opted-in development mode, it copies an
owned identifier from the target credential metadata. The native 8-byte header,
big-endian total length, ERANGE size-probe behavior, NUL terminator and copyout
fault handling are preserved. TEAMID and authentication results are unchanged.

The existing bounded Mach-O/SuperBlob reader now also accepts the primary
CodeDirectory identifier. It checks version-specific fixed headers, identifier
offset, nonempty NUL-terminated length up to 1,024 bytes and source bounds.
Unknown future versions, duplicate primary entries, malformed data and absent
identifiers fail closed. No filename-derived identifier or fabricated signature
state is used. The immutable credential object owns the original string and
queries return allocated copies, retaining the existing fork/exec/exit lifetime.

The full kernel and matching symbols, actual reader/object/lifecycle and csops
wire-format tests, nineteen-patch upgrade/idempotence and package validation
passed. The complete 23,094,320-byte BOOT prefix was read back with the expected
SHA-256 after installation, and reboot was requested. Runtime continuation
beyond the prior boot task requires the next device screen.

See [signing API inventory](signing-api-inventory.md) for remaining metadata
and authentication surfaces. Their presence in the source does not mean they
are all required by the current boot path.

## Device continuation

The next physical screen confirms that launchd starts real boot tasks and child
processes. MobileAssetEarlyBootTask completes and keybagd starts. The original
mount-task identity failure is passed. The new fatal condition is failure to
load the system keybag; keybagd requests recovery, processes terminate and the
HFS root unmounts. The final halt/restart timeout is a secondary platform reset
limitation, not the initiating keybag failure.
