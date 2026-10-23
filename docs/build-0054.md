# Build 0054: real touch transport binding and container response capture

## Recovery hardware evidence

Read the authorized beyond2lte recovery without mounting or writing USERDATA:
`/proc/bus/input/devices`, `getevent -lp /dev/input/event2`, I2C device links,
and existing kernel messages. The installed recovery probes sec_ts successfully:

- `sec_touchscreen` at `104b0000.hsi2c/i2c-4/4-0048`, input event2.
  Linux adapter 4 and the firmware SoC bus number 10 refer to the same MMIO
  controller; they are different numbering namespaces.
- Device ID `53 45 37 71 00`, application boot status `20`, functions `61`.
- Firmware name `tsp_sec/y771_beyond2_phole.bin`, installed image version
  17.56.2.33. Recovery explicitly skips the firmware update because it matches.
- ABS and multitouch positions 0..4095, ten Linux contact slots, pressure 0..63.
  Recovery reports display maxima 1439/3039. This verifies the established
  controller/address/coordinate assumptions, not XNU event delivery.
- Recovery's controller timing log: input 200 MHz, operation 400 kHz, FS1
  `0x01f0ff00`, FS2 `0x030003e0`, FS3 `0x00210000`.

No I2C commands, input injection, regulator changes, firmware update, unbind,
filesystem repair or journal replay were requested through recovery. Evidence
stays private in ignored out/logs.

## Concrete firmware preparation defect fixed

Reference UEFI branch `codex/touch-from-8549bfa` at
48add5bb03ab6f7ee4eb5aa04a30366e6dde6ba3, `yap.sh --touch`, enables BOTH
B2LTouchDxe in DXE.inc and the S5E9820 HsI2cLib DSC binding. B2L's preparation
had enabled only the former. The remaining commented binding selected
SamsungNullPkg's HsI2cNullLib, whose GetHsI2cBusData returns NULL and zero
buses. Consequently HsI2cDxe cannot provide the protocol required by
TouchDxe's DEPEX. This explains the previous EFI_NOT_FOUND diagnostic
protocol and zero uninitialized timings without blaming the sensor.

`prepare-boot-firmware.py --enable-touch` now enables and validates the exact
SoC library binding in the disposable firmware tree. Both edits are
idempotent, and a missing/ambiguous binding is rejected. No forced DEPEX,
APRIORI reorder, guessed timing, voltage, pinmux or sensor firmware change
was introduced. The actual linked HsI2cDxe static_library_files.lst now names
S5E9820Pkg/Library/HsI2cLib/HsI2cLib, rather than the null library.

The user's UEFI reference tree was not modified. All memory reservations,
6-CPU topology and four cache workers remain as in 0053. Real XNU touch
requires the next runtime handoff/probe and contact records.

## lsd / A5 comparison

A5's 0116 recorded valid container entitlements but the same CoreServices
systemGroupContainerURL abort. Its 0117 enabled the donor's HFS regular-file
RENAME_SWAP -> VNOP_EXCHANGE fallback; subsequent A5 logs showed graphics
presentation and no recurring container exception in that window. B2L already
has this fallback in 0037, and recovered 0053 logs show successful exchanges
and usermanagerd disk saves. Reapplying it does not address the remaining
B2L failure.

A5 also attaches entitlements to native cs_blob objects and sets internal
CS_SIGNED flags. B2L uses its existing explicitly enabled development metadata
adapter and approved query-only signature-presence reply. No additional
signing/trust flags, fake container path, privilege grant or success result
is added in 0054. The previous B2L metadata logs establish that lsd has its
system-container and manager entitlements plus readable original CDHash;
they do not establish a successful MCM request.

The read-only HFS catalog has the top-level container directories with root
ownership and 0755 mode; it contains no newly created system/group-container
children. Both MCM boot.txt files name the same boot UUID. Catalog evidence
can lag the unreplayed journal; this is not proof of a missing on-disk file
or a reason to create fake metadata.

### Diagnostic defect fixed

0053 admitted only 24 discovery messages per role/direction, then accepted
only IDs 0x10000000/0x10000001. Captured iOS26 CPX@ traffic also uses
0x400000cf and 0x20000000. Both MCM instances share a role, and repeated lsd
processes shared the exhausted counter. Thus the previous capture could
miss the actual container request and its error response.

0054 captures all message IDs within bounded per-process windows: up to eight
MCM processes (512 messages per direction each) and eight lsd processes
(256 each). Inline prefixes are at most 3072 bytes, at most one 2048-byte OOL
region is retained, and the entire boot has a 4 MiB raw-payload budget.
GUI admission remains at its existing smaller bounds. These are prefixes,
not a full private XPC schema or guaranteed request/reply reconstruction.

The same targeted syscall hook records successful and failed csops and
csops_audittoken results, including early-return CDHash cases that bypassed
the old kern_proc out-label trace. STATUS is copied only after successful
native output; a diagnostic copy fault is separate from the original result.
MCM/lsd syscall-error budgets are per process; expected mkdir EEXIST probes
have a separate small limit. Current-thread entry stack snapshots also cover
MCM/lsd. No IPC, syscall, authorization or signing result is changed.

This is a concrete touch build fix and a fix to the evidence capture needed
for lsd. It is not a claim that lsd's container abort has been eliminated.

## Verification / installation

- Firmware preparation suite: six cases pass, including real touch transport
  enablement and repeated preparation.
- Actual diagnostic C code passes ASan/UBSan: native copy faults/bounds,
  observed iOS26 IDs after discovery, independent process limits, and both
  successful STATUS and failed CDHash-like syscall results.
- Existing decoder fixture passes. Existing 42-patch prepared source accepts
  the synchronized overlay. Kernel and touch-enabled firmware build.
- HsI2cDxe actual link dependencies select the real S5E9820 library.
- Kernel/unstripped/dSYM UUID: 3E841801-6D76-341D-B6FC-FC8896CF3CD8.
- BOOT bytes: 23,320,056; SHA256
  `37a03974fa1a7ec4691961ecd283df04b25c447ec31e5dda516bbcb16210bef8`.
- Kernel SHA256 `578e644f91a07e14b57f3eb48525973442ba3c443303e56d34d884efb118fd13`.
- Firmware SHA256 `321fdf493cdb6b3fd1cb4c9269d806c80afde4299739135003df2fe56612851e`.
- Loader is byte-identical to 0053.

Confirmed authorized root beyond2lte recovery, exact BOOT sda14 and capacity,
and entire previous 0053 image prefix hash. Wrote only BOOT and matched the
complete 0054 image prefix SHA256. USERDATA's 4096-byte header was unchanged.
Reboot was requested. Retained 0053 is the matching rollback. SYSTEM and
USERDATA were not mounted or written from recovery. Device touch events,
lsd stability and a visible UI remain to be verified from this boot.
