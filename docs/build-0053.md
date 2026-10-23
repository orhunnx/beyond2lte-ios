# Build 0053: current-thread wait / container IPC diagnostics and touch handoff

Requested after 0052 produced no UI: obtain more evidence from SpringBoard,
backboardd and container requests, and use the UEFI touch branch for input.
No signing result, authorization result, native IPC message, syscall return,
CPU topology, storage/cache format or memory reservation is changed here.

## Targeted diagnostics

Explicit boot argument `b2l_diag=1` enables capture for the 64-bit processes
SpringBoard, backboardd, containermanagerd (including its truncated system
process name) and lsd. Ordinary unrelated processes are excluded.

- `B2L WAIT` / `WAITBT`: current-thread user PC and up to 12 user frames before
  potentially blocking Mach receive or BSD syscalls. Only SpringBoard and
  backboardd; at most 16 thread IDs per role, eight snapshots per thread, at
  least five seconds between snapshots. A thread that never returns leaves
  the entry snapshot, not periodic remote stack samples. Other threads are
  neither suspended nor read while running. A syscall entry identifies a
  boundary, not proof that the call blocked.
- `B2L IOKITWAIT`: matching class/name and timeout at native
  waitForMatchingServiceWithToken; first 64 calls per GUI role. The original
  matching operation and timeout remain.
- `B2L IPC`: PID, TID, role, direction, local/remote port names, message ID,
  bits, declared size and descriptor count. Send hooks run after native size
  validation; receive hooks run only after successful native message copyout.
  Vector sends use their validated message vector address/length.
- `B2L IPCDATA` / `IPCOOL`: bounded user-wire prefixes (1024 inline bytes and
  at most one 2048-byte OOL data region). Descriptors use the 64-bit user ABI,
  at most 16 are visited, with bounds and copyin checks. Unsupported descriptor
  types stop traversal. Port-array descriptors are not treated as byte data.
  Each role/direction admits at most 128 messages: the first 24 discovery
  messages, then libxpc IDs 0x10000000/0x10000001. Other discovery traffic does
  not consume later XPC admission slots. Total raw payload reservation is
  capped at 512 KiB for the entire boot. Copy failures and truncated prefixes
  are explicit; auxiliary vectors and additional OOL regions are not captured.
- `B2L SYSERR` / `SYSPATH`: first 128 failed BSD calls per selected role, their
  original errno and numeric syscall/argument information. A bounded printable
  path is copied only for known path-taking syscalls. Success returns are not
  logged, and no return value is changed.
- `B2L SURF OPEN/CALL` and `FB OPEN/CALL`: client-open and first 64 selector
  entries identify whether the existing bridge is reached at all. backboardd
  is also added to the bounded crash-backtrace process list.

Whole diagnostic lines reserve contiguous positions in the existing persistent
ring before publication. Formatting uses a caller-local buffer, bounded CAS
and no print mutex. Ordinary console lines retain their previous interleaving
behavior. A producer interrupted before publication can still leave explicit
missing byte positions; this is a lossy diagnostic log, not transactional IPC
storage. New verbose diagnostic lines are disk-only, avoiding framebuffer
render overhead for every payload chunk.

`scripts/decode-b2l-diag.py` reconstructs captured byte ranges, records gaps and
copy failures, and extracts printable tokens. It intentionally does not invent
an undocumented XPC schema. A message's PID-local port names and numeric ID
alone do not establish cross-process request/reply identity. Capture may expose
the container identifier/error in payload tokens, but complete semantic request
and error decoding still depends on the next actual capture. Raw payloads and
private log contents remain local under ignored out/.

Example after the next recovery capture:

```sh
python3 scripts/pull-b2l-log.py --serial R58M33609TR --adb /opt/homebrew/bin/adb --output out/logs/0053-recovery-first.txt
python3 scripts/decode-b2l-diag.py out/logs/0053-recovery-first.txt --output out/logs/0053-diagnostics.json
```

## UEFI touch-branch comparison and changes

Read the reference branch `codex/touch-from-8549bfa` at
48add5bb03ab6f7ee4eb5aa04a30366e6dde6ba3. Its B2LTouchDxe sources match those
already staged in the disposable firmware workspace; the reference repository
and its unrelated untracked files are not changed.

The previous native port omitted controller reset before command/read pairs,
trailing count configuration, the device-ID/integrity probe and the sec_ts
clear-events / touch-functions / sense-on sequence. This build follows that
branch's sequence at the established HSI2C10 / 0x48 address:

1. Reset the mapped controller, select master/auto mode and trailing count
   0x00ffffff before each command or command/read pair.
2. Reuse actual firmware timing registers. No guessed timing, pinmux, voltage,
   PMIC power cycle or touch-firmware update is added in XNU.
3. Probe device ID (0x22), firmware integrity (0x21) and boot status (0x55).
   Require the application-mode 0x20 and integrity bit 0x80 before sensing.
4. Clear events (0x62), set touch/palm/wet functions (0x30, 0x61, 0x00), then
   issue SENSE_ON (0x10). Probe transfers allow the branch's bounded 100 ms;
   runtime transfers are bounded at 20 ms. Every native success/failure exit
   quiesces the channel/interrupt source. Three failed event polls disable
   touch while preserving GPIO buttons.
5. Log the exact failing stage and bounded transfer snapshots (command,
   direction, byte count, controller/interrupt/error/transfer/FIFO/address and
   timing registers). Registration or a successful build is not touch proof.

The loader now reads the real revision-5 B2LTouchDiagnostic protocol and passes
its compact versioned status snapshot in `/chosen/b2l,touch-status`. It contains
firmware locate/init/probe/reset/functions/pointer/power results, boot status,
timings and the last transfer error registers. XNU persists it before native
probe. This separates an un-dispatched/failed firmware driver from a native
transport failure after ExitBootServices. Protocol ABI is copied from the
named reference branch in TouchDiagnostic.h; no firmware memory pointer is
passed to XNU. The snapshot precedes ExitBootServices and is not a continuous
or final-handoff hardware trace.

## Checks and installation

- Actual diagnostic C implementation passes ASan/UBSan host checks: role and
  boot-argument gating, current-thread sample interval/cap, receive lengths,
  descriptor types/counts/overflow, invalid user addresses, global byte budget,
  discovery/XPC admission, original errno and bounded path capture.
- Existing persistent log test plus eight concurrent bulk producers passes
  ASan/UBSan, including no mixed bytes inside fully published reserved lines,
  sequence limits, framing, wrap and storage failures.
- Loader tests pass including the new 24-word touch-status property and
  existing DT capacity/geometry/reservation checks. Decoder fixtures preserve
  exact bytes, printable tokens, missing ranges and copy faults.
- Actual 42-patch preparation passes upgrade from 41, repeat/idempotence and
  local-edit refusal. Final prepared source matches the overlay.
- Kernel and touch-enabled firmware build. Object symbol references confirm
  IPC, syscall and IOKit hooks really call the new diagnostics.
- Kernel/unstripped/dSYM UUID: CAB58A00-1555-34AB-8AC3-736232C673C8.
- BOOT bytes: 23319992; SHA256 `216447fabe0b943b256387a8e3c11a96797548d06ca22e500b114244fe636a2d`.
- Kernel SHA256 `a6c215e38b31357fbdd2671d649ae2b352f8524432991b171d3ae696db839ff2`.
- Firmware SHA256 `2ff9552cd40d981985f309fc1b59e1b0c716ce7297a05eb8750a848025f7efb9`.
- Loader SHA256 `73efb2307d8c9152f7aeb327436854fd3eb84e981cb5e22bb0865eb1d2293cd9`.

Confirmed rooted authorized beyond2lte recovery, sda14 and its exact capacity,
then checked the entire current 0052 BOOT prefix and the local rollback image.
Wrote only BOOT and verified the entire 0053 prefix SHA256. USERDATA's 4096-byte
header was unchanged. SYSTEM and USERDATA were not mounted or written from
recovery; the previously captured 0052 logs remain local. Reboot was requested
successfully. Retained 0052 is the matching rollback. Runtime diagnostic output,
touch operation and UI appearance still await the next device observation.
