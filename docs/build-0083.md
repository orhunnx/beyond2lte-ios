# 0083: extension cache admission, digitizer ABI, elastic sandbox and iPhone identity

## Evidence from 0082

The complete current-boot native export contains 5,658,151 events. sharingd and
DataMigrator account for 90.277% of events (not a measured CPU percentage).
LSd reaches the pinned AMFI selector6 successfully but SecCodeMapMemory reports
100001 while pushing the detached extension-cache signature. Native Security
uses F_ADDSIGS (59) for that detached CodeDirectory. The normal B2L blob path
has no supported entitlement-context adjustment. Killing DataMigrator would
leave the extension discovery failure unresolved.

The backboardd crash asks IOKit for DigitizerGenerationCount (0xb001b). The
native getter at unslid 0x197eb8ba4 reads data+0x90; the actual fault address is
exactly that offset into a malloc guard. Our legacy digitizer structure is only
104 bytes. The shipped native digitizer creator allocates 160 bytes and stores
binary64 coordinates at 0x10/0x18/0x20, integer fields at 0x28..0x3c and pressure
at 0x40. This is a measured wire-format mismatch, not a reason to disable touch.

The persistent log explicitly reports Photos file-extension issuance error28
(ENOSPC). The old global registry has only256 token entries. Assetsd subsequently
reports TCCErrorDomain6 and throws because it cannot obtain its Photos capability.

## Changes

* Patch0052 handles only F_ADDSIGS for the exact extensions.plist path, original
  98,192-byte file SHA256 and one of two exact detached CodeDirectory SHA256
  pins. Caller must be64-bit, platform-classified, in B2L development mode and
  carry the existing cache-query entitlement. Unknown files retain the original
  kernel path. Changed bytes and malformed inputs do not receive admission.
  This is local unsigned-development data admission: no cs_blob, trusted VM
  pages, new entitlement or Apple signature-chain verification is fabricated.
* Digitizer serialization now uses the native160-byte layout. Legacy fixed-point
  internal fields are converted to IEEE754 bits with integer instructions.
  Length calculation, child serialization, createBytes and incoming decoding
  share that contract. Integer-only event types retain their bytes. Generation,
  will-update and did-update fields no longer read beyond the wire allocation.
* Sandbox token/handle registries grow in stable chunks, allocated outside the
  spin lock and published under it. Existing IDs, ownership, target versions,
  reference counts, copy rollback and exit cleanup are preserved. Released
  slots are reused; chunks remain available until reboot. A memory ceiling of
  16,384 tokens/65,536 handles (~21MiB) bounds the provider, while startup uses
  only64/256 entries. Allocation failure returns ENOMEM rather than destroying
  existing capabilities. No token eviction or TCC blanket-allow is introduced.
* On the owner's explicit request, root model/hw.product/hw.machine is
  iPhone17,2 and product metadata says iPhone16ProMax. The actual Samsung
  compatible and board-config remain for B2L hardware/driver matching. Native
  MobileGestalt reads hw.product, recognizes the iPhone prefix and returns
  DeviceClass iPhone; previously the unknown Samsung product fell back to iPod.
  The native HomeButtonType handler reads /buttons/home-button-type as a
  four-byte number; value2 means no physical Home button. Missing data defaulted
  to mechanical0. No serial, ECID, IMEI, chip identity or production status is
  invented by these software product metadata changes.

## Verification and limits

Actual-code ASan/UBSan checks cover both CodeDirectories, every one-bit mutation,
changed file bytes, caller/path/offset/size gates and copy/read/allocation faults.
Sandbox tests cross the previous limits, issue Photos after1024 tokens, exercise
allocation failures, publication/exit races and8 concurrent threads. The actual
legacy digitizer struct and codec pass native offset/length, unaligned buffer
canaries, binary64 coordinates, numeric round-trip and unchanged integer-event
checks. DT capacity/topology/ABI tests check iPhone17,2 and HomeButtonType2.
XNU and EFI builds pass. The linked kernel must retain zero RCpc instructions;
the measured M4 identity, eight-CPU startup and four cache workers stay enabled.

Only BOOT is installed with a full0082 backup, verified prior prefix and exact
readback hash; private WiFi metadata stays in ignored output. The flash receipt
is out/0083/flash-receipt.json. Runtime acceptance, disappearance of the retry
storm, Photos startup, touch stability and the displayed iPhone identity require
the next device run. Other0082 crash families are not claimed fixed here.
