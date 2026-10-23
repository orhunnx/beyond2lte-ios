# Build 0065: TCC file capability compatibility and late UI evidence

## Current runtime evidence

The read-only 0064 recovery capture is epoch 31, 3,213,583 bytes from sequence
zero, with no sequence gaps, missing positions or producer drops. The previous
appplaceholdersyncd missing-container-token breakpoint is absent (0063 had 16
breakpoint events). An idle-demotion timeout for that process remains; absence
of the old crash does not prove all of its work completes. Native container
file token issue and consume calls now return success.

Assetsd still aborts at its native Photos preflight exception path. Existing
original metadata includes Photos allow membership and a 5,732-byte DER blob.
The read-only TCC.db snapshot has no relevant Photos/PhotosAdd/AddressBook or
assetsd access row; this alone neither proves an authorization nor a denial.
No new .ips report was recovered: the catalog contains the same 59 selected
reports as the previous capture. Older .ips files must not be attributed to
0064.

The log repeatedly records tccd's exact file class
com.apple.tcc.kTCCServiceAddressBook returning ENOTSUP (45). Native tccd issues
this file capability with resource / and flags zero. The prior registry also
rejected / as a file resource, so adding only the class would still fail.
0065 accepts that exact class and root resource in the existing explicitly
unconfined development mode. Root path release matches only that class and
the current consumer owner. Unknown classes and generic type 3 stay unsupported.
No Photos authorization, entitlement or signing trust is fabricated.

## UI boundary

SpringBoard and backboardd have no captured crash/exit. Only three framebuffer
swaps (surfaces 2,3,2) occur, including successful CPU copy to physical scanout.
SpringBoard main-thread sample at second 137 is in CADisplay display discovery
through CARenderServerGetServerPort and bootstrap lookup of
com.apple.CARenderServer. Backboardd main-thread samples at seconds 107–127
are inside CAMetalContextCreate's IOAcceleratorES matching probe. Native code
limits that probe to approximately 30 seconds; these snapshots do not prove a
permanent GPU block. The backboardd launch plist already declares the render
server Mach service. Selector 72 notification enabling is not reached in the
capture, though its native ABI matches the current shim.

Early diagnostic limits hide the later handshake. 0065 keeps bounded separate
GUI bootstrap request/reply quotas, endpoint/trailer captures and fresh late
GUI sampling windows at 180/300/600 seconds. TCC IPC for exact assetsd/tccd
names has separate per-direction and byte budgets, so early tccd traffic cannot
consume assetsd's later capture allocation. These diagnostics preserve all
native IPC and syscall results.

## Build and verification

The previous TCCCS/TCCMAC diagnostic source was missing from the actual 0063
and 0064 object/images. Recompiling the current source reproduces the expected
markers. 0065 explicitly rebuilds b2l_diag.o, and both diagnostic markers, the
new exact class, and the late-window format were checked in the actual Mach-O
before packaging. Prior absence of TCCCS records cannot be interpreted as
absence of native csops calls.

Actual registry tests and targeted diagnostic tests pass ASan/UBSan, including
class/root restrictions, ownership, copy failures, forged/stale/bound tokens,
concurrency, independent TCC budgets, malformed payloads, and late GUI windows.
The real DEVELOPMENT ARM64 B2L kernel builds successfully.

0065 is 23,373,128 bytes, SHA-256:
79737aff531a2035eb7be2db5377a80b5dc49a1991ca249da040dee79bcc707e.
The verified 0064 Android firmware/loader prefix is reused byte for byte.

Installation is pending the S10+ recovery USB connection. The next runtime
capture must establish the actual Photos authorization response and render
server discovery completion. This build is not a claim that assetsd or the
home UI is already fixed on the device.
