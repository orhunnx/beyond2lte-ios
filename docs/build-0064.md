# Build 0064: native container extension classes

## Evidence and fix

The complete CRC-checked epoch 30 capture in out/logs/0063-recovery contains
16 appplaceholdersyncd breakpoint events with paired exception-10 records.
Detailed crash snapshots state "Sandbox token not sent from container".
Container-manager Sandbox operation 5 returned ENOTSUP (45). The development
registry supported only app-sandbox read and read-write classes, whereas the
shipped iOS 26 ContainerManagerCommon plists request four different classes:

- com.apple.sandbox.container
- com.apple.sandbox.application-group
- com.apple.sandbox.system-container
- com.apple.sandbox.system-group

The registry now accepts those exact file classes and retains the requested
class ID in each capability. Existing development-mode and 64-bit-process
gates, audit-token PID/version binding, ownership and refcount rules remain.
Unknown names/types and release-last remain unsupported. This is bookkeeping
for the existing unconfined development system, not production sandbox VFS
enforcement or Apple token authentication. Signing and entitlement metadata
are unchanged.

Bounded B2L EXT ISSUE and B2L EXT RESULT diagnostics distinguish class/type,
issue, consume and release results without exposing token nonces or paths.

## Verification and installation

The actual registry host test passes ASan/UBSan, including all four native
classes, exact-name rejection, bound-target transfer, owner/refcount and
copy-error rollback, stale/forged tokens, exit cleanup, capacity and eight-thread
concurrency. The DEVELOPMENT ARM64 B2L kernel builds successfully.

0064 reuses the verified 0063 Android firmware/loader prefix byte for byte.
BOOT is 23,372,744 bytes, SHA-256:
da72f2a89430e1ece6d14991119fb35d2107a75d92390361d1dc66d91b28ddba.

The connected R58M33609TR beyond2lte recovery identity and 57,671,680-byte BOOT
capacity were checked. Full previous BOOT was backed up and its image prefix
matched 0063. Only BOOT was written; full image-prefix readback matched SHA-256
and reboot was requested. Receipts, backup, image and matching debug symbols
are retained under ignored out/0064.

Runtime removal of the appplaceholdersyncd crash requires the next capture.
Assetsd/TCC remains a separate unresolved issue; this build does not claim
complete UI bring-up.
