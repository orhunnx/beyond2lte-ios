# Build 0031: persistent console, bounded larger RAM and launchd cache admission

## Observed boundary

The 0030 screen reaches launchd_cache_loader, which aborts while matching the
AppleMobileFileIntegrity IOService. The kernel development metadata adapter is
not that userspace IOService. Local analysis confirms the current iOS loader's
existing `launchd_unsecure_cache=1` development option bypasses detached-signature
and AMFI trust-cache lookup, then uses the normal cache mapping/send path. This
is also the A5 bring-up solution. It does not register a fake trusted service or
claim that the launchd configuration signature was authenticated. Expected next
messages are `Using unsecure cache` and cache delivery to launchd.

## Storage

Recovery inspection finds clean journaled SYSTEM with only its internal HFS
metadata/journal entries, and no iOS files. Its physical extent is 5,976,883,200
bytes. The user authorizes replacement for the auxiliary development store:

| Relative range | Purpose |
| --- | --- |
| 0..5,960,105,984 | Empty Journaled HFS+, volume B2LCACHE |
| 5,960,105,984..5,976,883,200 | 16 MiB typed, CRC-framed persistent log |

The HFS image passes real fsck through a virtual device of its own exact length.
The complete SYSTEM transfer is readback-hash verified. Existing USERDATA/root,
GPT and the original Android backups are unchanged. No new backup is taken.
The filesystem is cache capacity prepared for later use; no translation engine
or persistent executable translation cache is claimed by this build.

The log sink requires the unique SYSTEM GPT entry, an explicit USERDATA root,
exact shortened HFS geometry in both volume headers and valid typed log headers.
It writes through a separate private reader whose only authorized extent is the
16 MiB tail. Existing BSD write authority remains restricted to the selected
USERDATA root. The SYSTEM BSD view excludes the log tail and remains read-only.
The whole-LU alias never gains write access.

See [persistent console](persistent-console.md) for framing, concurrency and
recovery extraction. The worker emits a bounded progress line about every 30
seconds: emulation trap/completion counts, last best-effort PID/PC and log-write
errors. These relaxed samples are telemetry, not a coherent thread stack or a
proof that a repeated instruction makes useful progress. Increasing counters
distinguish continuing emulation; a heartbeat also demonstrates worker wakeup.

## RAM and CPU

The live recovery map identifies the 8 GiB variant and its additional 40 MiB
EL2 early-memory reservation, which the bundled static DTS omits. The disposable
XNU firmware profile keeps all 25 live fixed reservations, the lower guards and
first upper-bank guard. It exposes only the safe higher span and reserves 32 MiB
EFI headroom, yielding at most **4024 MiB** XNU RAM. Actual EFI fragmentation may
reduce this. The loader verifies allocated/final map ownership, and the bootstrap
table budget grows from 10 to 16 pages. See [RAM profile](ram-profile.md).

This package retains one CPU. A separately staged two-core candidate needs a
clean MAX_CPUS=2 build and real secondary-entry, per-core timer and bidirectional
IPI proof. It is not silently enabled by changing the command line.

## Verification

Actual-source ASan/UBSan checks pass for concurrent console publication, ring
wrap, framing/CRC, epoch rotation, torn writes, synchronization errors and decoder
interoperation. The auxiliary layout tests reject overlap, ambiguous labels,
invalid geometry and full-size old HFS headers. Existing UFS root-write, RMW,
strategy and flush tests pass, including the shortened read-only SYSTEM view.
Runtime-wrapper permission gates and early-console bounds/one-append tests pass.
Arena tests, five firmware-profile tests, four RAM/bootstrap tests and the
21-patch upgrade/idempotence/local-edit-refusal test pass. Both full kernel and
EFI/FD builds, kernel inspection and embedded-loader/package checks pass.

The native recovery reader sees the provisioned epoch-0 region with zero records
before the new boot; this tests extraction, not runtime logging. The complete
SYSTEM transfer and the 23,147,456-byte BOOT prefix are readback-hash verified.
BOOT SHA-256:
`a4d543f40b78af2afeabe944b091fe80bb84a7ba38e59f589c2c717b1f53c287`.
Stripped kernel, unstripped kernel and preserved dSYM share UUID
`98E01992-1BEC-31A6-B7D1-3BDFB785E2C2`. Reboot was requested after both checks.

Device runtime proof still requires the next screen and a recovered log record.
Persistence can lose the final seconds before a panic or disk fault.
