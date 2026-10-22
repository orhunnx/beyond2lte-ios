# Build0049: disk-backed cache and CodeDirectory query metadata

## Device evidence before this build

0047 proves six CPU timers and bidirectional SGI1. 0048's epoch 14 log proves
four concurrent preparation workers, peak 4, CPU mask 0x3f, all completion
counters advancing, and no cache I/O/flush error. The128 MiB cache was full at
5461 pages; queued is cumulative and not the outstanding ring length.

## Cache changes

SYSTEM's existing HFS volume is retained. A local full-partition clone grows
only its private cache file to 5,929,500,672 bytes (about5.52 GiB),241,271 slots,
four validated inline extents. All 5,461 original dual-copy cells compare
byte-for-byte with the saved V1 file. The 16 MiB log tail is unchanged. Firmware
stages only 8 KiB headers, never the whole file. XNU keeps 64 hot cells (1.5 MiB)
plus the key/hash index (about 6 MiB for this disk layout); durable cells can leave
the hot window without losing undo information. The source code is changed
only by the existing pre-init VFS apply path after native HFS journal replay.
RAM reservations and the corecrypto measured-text exclusion are unchanged.

V2 has 64-bit disk geometry and alternating generation/high-water headers.
Record write, flush and readback precede header write, flush and readback;
RAM publication is last. Interrupted learning may lose the newest uncommitted
observation. It cannot retire the original of a previously applied page.
Record CRC/SHA, sequence checks, A/B copies and exact readback remain mandatory.
Recovery never mounts/replays/writes USERDATA; only its header is read to
check that the staging/install procedure did not change it.

## Crash work

The repeated lsd abort resolves to failure to obtain the user container URL.
ManagedSettingsAgent/appprotectiond deliberately trap at SystemLocations.swift
line 37 after failing the shared group container query. mobileactivationd has
an explicit fatal NULL check on its container paths. These are not unsupported
instruction faults. ContainerManagerCommon consumes CDHash/identifier/team
metadata; the development shim supplied identity but left CDHash queries to
native cs_blob lookup even though there is no authenticated cs_blob.

This build supplies the SHA256/truncated-SHA256 digest of the original primary
CodeDirectory through CS_OPS_CDHASH and CS_OPS_CDHASH_WITH_INFO, preserving
native target, access and buffer-length checks. The metadata object owns an
immutable retained copy. This does not authenticate the signature or code page
hashes, add CS_SIGNED, create a fake trust chain, or promise the container
failures are completely resolved. That requires a new device log.

cryptexd reaches libimage4's deliberate fatal helper; the exact formatted
chip/configuration error was not in the old sampled backtraces. passd also
crashes in 0048. Add both plus appprotectiond to the bounded crash sample list,
log saved user registers and at most three 256-byte printable strings through
copyinstr. There is no process heap scan. This is diagnosis for remaining
failures, not a claim that missing Apple secure hardware is implemented.

## Checks

- Actual 35-patch sequence on the pinned source: incremental application,
  idempotence and refusal of unrelated edits pass.
- Metadata/CDHash/credential-lifetime tests pass ASan/UBSan, including malformed
  input, unsupported hashes, I/O failures and immutable retained copies.
- Actual disk-backed store: all torn-header prefixes, interrupted record/header
  publication, bounded RAM eviction and reopen/reindex pass ASan/UBSan.
- Expanded real HFS fixture: 5,461/5,461 cells unchanged; cross-extent writes,
  metadata overlap and out-of-range accesses checked without writing the image.
- Four actual worker bodies: same-page merge, distinct allocation, duplicate,
  full cache, corecrypto exclusion and stop-on-I/O-error pass ASan/UBSan.
- Kernel and embedded EFI firmware build; final cache/emulator/CPU object
  scalar code generation checked. Physical sudden power loss is not tested.

Kernel UUID E1E85E4A-231D-3C39-BECA-562905832399 matches stripped/unstripped/dSYM.
BOOT SHA256 caa2666dd306f89905ed401312c99d92cef8730f7ea777f0fb21ebe6fb19cca5.
Installed from authorized recovery after matching the entire current SYSTEM
against its saved full backup and matching the current BOOT to 0048. Complete
written SYSTEM readback SHA256:
`0b59203178d7acea1aceba14a198d0a0a2d6f99aca6bec5e339a1f6929d8b733`.
The complete BOOT prefix also matches its manifest; the USERDATA header is
unchanged, and reboot was requested. Saved SYSTEM backup plus 0048 BOOT form
the matching rollback pair. The older known 0039 BOOT is retained as well.
Userspace crash resolution and 0049 capacity growth were initially runtime-unverified.

## Recovery runtime observation (2026-10-23)

The next read-only recovery capture has epoch 15, 1,257 valid log records,
1,539,146 decoded bytes, one missing byte position and zero producer drops.
Some console lines interleave across CPUs, so only intact fields are treated
as evidence. There is no kernel panic in this captured log.

- All six CPU timers and bidirectional SGI1 pass again.
- Native cache reports 5,654 MiB, 241,271 slots, 5,461 initial used cells,
  1,536 KiB hot cells and 5,817 KiB index. Pre-init applied 1,081 pages and
  found 4,380 already translated, with zero skips.
- Four workers are active with peak 4 and CPU mask 0x3f; all completion
  counters advance. Last intact counters: queued 1,238,992, learned 88,092,
  skipped 72,361, dropped 7,115,157, errors 0. Flush errors remain zero.
- Direct read-only A/B header capture verifies generation 13,639 / 13,638
  and used 19,099 / 19,098, both with valid CRC. Thus 19,099 unique page
  cells are durably committed, beyond the old 5,461-cell limit. learned
  counts record updates, not distinct cells. New cells since migration: 13,638.

The CDHash query fix does not by itself resolve the observed crashes:

- lsd retains the CoreServices abort callsite for failure to get a user
  container URL.
- ManagedSettingsAgent retains the SystemLocations.swift shared group
  container fatal callsite.
- AppProtection's actual caller is APContainer.swift, line 94, with the
  message prefix "Couldn't get main container: ". It is a distinct caller
  from ManagedSettings despite the same shared Swift assertion helper.
- mobileactivationd retains its container-path fatal check.
- cryptexd now captures the exact libimage4 message "panic: failed to get
  product node". Its trace reaches img4_chip_select_personalized_ap. Local
  callsite inspection confirms the failing IORegistry path is
  IODeviceTree:/product; the generated board device tree lacks that node.
  Other paths queried nearby are /chosen/manifest-properties and /chosen/asmb.
  Adding a node alone is not proof that later Image4 chip requirements work.
- passd resolves through swift_unexpectedError from
  FinanceKit/CoreDataProvider.swift, line 34. The underlying thrown error
  is not printed in this capture and remains unresolved.

SpringBoard executes in this log, but no SpringBoard crash record appears;
that does not establish working UI. Repeated service launches continue through
PID 2,069. The expanded cache and SMP are device-verified; userspace service
crashes and UI bring-up remain open. This inspection performs no device writes.
