# Build 0036: bounded crash reasons and user backtraces

## Recovered 0035 evidence

The recovery capture contains 181 valid persistent records and 274101 text
bytes from epoch 8, starting at sequence zero without gaps or producer drops.
The final saved line is incomplete, so the RAM tail is not established.
The exception logging tunable is active. FIPS aggregate result is true and
launchd completes early boot. The FIPS success line interleaves with a cache
status line; retain the original raw capture when interpreting it.

SpringBoard repeatedly terminates with SIGABRT, while securityd and
ManagedSettingsAgent repeatedly reach Swift assertion traps. UserEventAgent
exits with a libsystem reason. The precise SpringBoard abort cause is not yet
identified. The native system-event payload is only 96 bytes, which truncates
exit descriptions after their long headers despite the logging tunable.

Read-only executable samples show no unexpected disk differences: SpringBoard
and mobileactivationd still match the staged originals, and restoring recorded
native word differences in securityd, usermanagerd and watchdogd reproduces
the originals. This does not prove translation semantics or all shared-cache
pages correct. The mobileactivationd breakpoint maps uniquely to its error
helper for failure to copy required container paths; the exact missing path
and the relationship to other crashes remain unproven.

The 5461 cache slots are occupied; all nonempty sampled copies validate and
there are no duplicate page keys. Existing entries can be updated, but new
pages cannot be admitted. Original page records remain necessary for undo.
The dirty USERDATA root was not mounted or replayed in recovery.

## Change

Board patch 0024 adds bounded `B2L CRASH`, `B2L REASON` and `B2L BT` records
before the existing exit processing. It uses the already enabled exception
logging parameter, matches six process-name prefixes, and admits four crash
samples per class. Reason descriptions are bounded to 256 bytes and user
backtraces to 12 frames through the existing XNU backtrace_user API. It only
samples the current process with a nonzero exit status or exception.

This diagnostic update does not establish a fix for the SpringBoard crash.
The existing exception behavior, emulator, persistent cache, FIPS exclusion,
CPU topology and firmware reservations are retained.

## Verification and installation

The incremental DEVELOPMENT kernel build and the real 23-to-24 patch sequence
pass, including idempotence and local-edit refusal. Existing scalar codegen
checks pass for the native cache and ARM64e compatibility objects. Matching
kernel, unstripped kernel and dSYM UUIDs are retained locally. The EFI firmware
and loader are reused from 0035; the package verifies the embedded loader.

BOOT size: 23166824 bytes. SHA-256:
`97722d745d0e287e35d2abf4aa93d0a72337cdcc46cd9d3de99b5452f78236c7`.
The installed 0035 BOOT prefix was verified before writing; the complete 0036
image prefix readback matched after installation. Reboot was requested.
No SYSTEM or USERDATA image was rewritten. Runtime diagnostics and graphical
startup still require the next device capture.

Private logs, binaries, cache contents and addresses remain ignored locally.
