# Build 0066: confirmed Photos token failure

## 0065 recovery findings

Epoch 32 contains 5,061,470 bytes from sequence zero, no sequence gaps, no
missing positions and no producer drops. The read-only HFS catalog extraction
also recovered a new assetsd report in boot A328A299-F4DF-4D6E-8B4F-1BF537D830F8:
assetsd-1969-12-31-160821.000.ips, uptime430, SIGABRT at the same native
Photos-preflight exception path.

AddressBook token issue now succeeds. Photos token issue is the next observed
missing class, not a speculative permission addition:

- tccd pid40 issues com.apple.tcc.kTCCServicePhotos, file type0, flags0.
- The development registry returns ENOTSUP45 twice.
- Complete TCCD CPX replies3181/3187 explicitly identify com.apple.assetsd,
  pid272, and say "Failed to issue Sandbox Extension for kTCCServicePhotos".
- Typed reply fields are auth_error_code(int64)=6, auth_value(int64)=0,
  result(bool)=false, auth_reason(int64)=1, auth_version2/1.
- The second reply is immediately followed by assetsd272 SIGABRT.

Original assetsd DER5732 and identifier op11 size26 are delivered successfully;
this is not evidence that those original entitlements or identity are absent.
The existing appplaceholdersyncd token breakpoint is absent, although its
idle-demotion timeout remains. No familycircled exception is captured.

0066 adds only the exact Photos file class and its native / resource, extending
the existing AddressBook root-resource helper to those two exact classes.
Owned handle/path release remains restricted to the matching classes and owner.
PhotosAdd and generic type3 remain unsupported. The native TCC authorization
computation and original entitlements/signing metadata are unchanged.
Causal byte digests, typed replies and ordered log lines are retained locally
in out/research-0066/assetsd-replies.json.

## Home UI evidence

There are still three framebuffer swaps and no selector72 notification enable.
SpringBoard and backboardd have no captured crash/exit. New GUI IPC proves
CARenderServer lookup succeeds (SpringBoard sequence867 request, sequence868
complex reply with port0x2a87; another reply yields0x2ec3). SpringBoard also
performs filesystem calls after the last early bootstrap WAIT record. Thus
that WAIT is not proof of a permanent bootstrap block or missing service.
Backboardd progresses beyond the finite Metal probe and later appears in a
CoreMotion/dispatch/kevent_qos chain. Native kevent return values are not visible
in the old exhausted generic error quota.

0066 gives the exact native display-list RPC request0x9d27, reply0x9d8b and
send-once notification0x47 separate bounded IPC/endpoint/receive quotas and
raw-byte budgets. Native GetDisplays uses synchronous SEND_MSG|RCV_MSG,
36-byte request and untimed receive; successful reply is complex56B with one
OOL descriptor. Error replies carry RetCode@32. A notification ID by itself is
not classified as a failure. GUI event-loop completion diagnostics preserve
actual native success/error results with separate current-thread/window limits.
No render response, event or service is fabricated. Missing home UI remains
an open runtime diagnosis distinct from the confirmed assetsd failure.

## Verification

Actual extension registry and targeted diagnostic tests pass ASan/UBSan,
including exact Photos root-resource lifecycle, ownership/path-release scope,
unknown-class/type rejection, concurrency, independent render request/reply/
notification quotas, and event-loop success/error capture. The real DEVELOPMENT ARM64 B2L kernel builds successfully. The actual Mach-O
contains the Photos class, TCC result and event-loop diagnostic markers.

0066 BOOT is 23,373,328 bytes, SHA-256:
83fbd62e1606a10f677da4300a072cda1f8df9513700c1a8763a905d2421cae1.
The verified 0065 Android firmware/loader prefix was reused byte for byte.
Connected beyond2lte recovery identity and BOOT capacity were checked. Full
previous BOOT was backed up and matched 0065. Only BOOT was written, full image
prefix readback matched SHA-256, and reboot was requested. Private images,
matching debug symbols, backup and receipt remain under ignored out/0066.
Runtime Photos preflight success and home UI progress require a new capture.
