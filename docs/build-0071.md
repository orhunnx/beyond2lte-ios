# Build0071: reject unsupported system sleep; report no-passcode state

## Runtime evidence from0070

The user observed SpringBoard's lock/passcode UI, then a panic. The photograph
identifies `B2L CPU: halt is not implemented` at B2LCPU.cpp311. The persistent
kernel log independently records `Sleep Reason: Idle Sleep` twice before its
last committed records. Full CPU suspend/resume is not implemented on B2L.

0070 epoch38 exports5,564,554 console bytes from sequence0 without missing
positions or producer drops. Its native archive contains629 files/787,581,795
bytes; all selected files are complete and the source HFS header is unchanged.
The changed Persist files decode to2,443,759 events from one current boot.
SpringBoard has9,996 events, including27 `Failed to get device state` messages
and actual passcode-controller presentation. Native WiFi-address unavailable
and generic network-match-timeout counts both fall to zero after0070;257
SysCfg failures in other queries remain. Framebuffer swaps advance beyond the
three startup frames. This confirms UI progress but not stable home-screen use.

## Sleep capability

The loader publishes native uint32 `sleep-disabled=1` in `/defaults`.
`PE_get_default` reads this exact DT path; IOPMrootDomain consumes it when system
boot completes, before accepting system-sleep transitions. The existing native
check rejects sleep rather than reaching IOCPUSleepKernel's unsupported halt.
No halt/resume function is changed to pretend it succeeded. Display blanking
and interrupt-driven CPU idle are separate. Deep system suspend remains absent;
the development system can continue consuming power while the display is off.
The kernel prints a bounded platform marker identifying this configuration.

## Why passcode appeared

The existing volatile development KeyStore represents an already-loaded,
unlocked system bag0 without a password. Selector7's scalar4 is the native
unlocked-since-boot bit; it is not the newer detailed lock-state enum.

SpringBoardFoundation initializes its state with selector35 through libaks.
The old shim returned unsupported. Native code zero-initializes its decoded
structure, logs the error, and retains zero fields; its cached `hasPasscode`
comparison treats every detailed lock-state except enum3 as passcode-present.
Enum3's native description is **Disabled**. This accounts for a passcode UI
without a password having been created in this development service.

## Read-only metadata correction

Selectors17/35 now accept only an opened connection, one scalar handle0,
no structure input, no scalar output, and sufficient structure output capacity.
They return exactly12 bytes of DER:

`SET { SEQUENCE { UTF8String "sls", INTEGER 3 } }`

The native client decodes DER, rather than interpreting a raw66-byte structure.
Its decoder initializes omitted optional fields to zero and does not require
all keys, so only the proven `sls` field is emitted. No undocumented overall
state, UUID, keys, SEP capabilities, encryption or serialized keybag is invented.
Other bag handles and unsupported cryptographic operations still fail. Existing
B2L development gates and scalar7 behavior remain intact. No passcode, keybag,
preferences or user-data file is modified by this correction.

Static native anchors: libaks dictionary key0x1e4b42b8a contains UTF8 `sls`;
extended decoder clears0x54 bytes, numeric decode returns0 for absent fields,
and SBF's lock-state mapping preserves enum3 before its hasPasscode comparison.
Focused disassembly and current-boot records remain in ignored
`out/research-0071/passcode*`.

## Checks and installation boundary

ASan/UBSan checks exercise the actual portable core, actual C++ adapter and
complete mocked service/client. New cases cover12-byte DER, native4096-byte
output capacity, guard bytes, short capacity, descriptor short writes, bad
handles and invalid argument shapes. The DT decoder verifies sleep-disabled
has the correct path, uint32 length and value. Real XNU and EFI/firmware builds
pass. Packaging validates the embedded loader and keeps the verified factory
WiFi address only in ignored private output. No actual address is in source,
documentation or a public manifest; no GitHub push is performed.

Only BOOT installation is authorized/performed in this sequence, with a full
0070 backup, matching prior image prefix and exact0071 readback hash recorded
in `out/0071/flash-receipt.json`. Runtime acceptance of the new DER state and
absence of another sleep panic require the next device observation. Separate
assetsd and speech-service crashes remain in0070 and are not claimed fixed.
