# Build 0097: identify the critical exit and accept native cache ownership

## Changes

The MobileGestalt cache on this image belongs to uid 501, not root. The scoped
one-time profile transition now accepts uid 0 or 501 while retaining the exact
path, development-mode gate, regular-file, single-link, size and exclusive
rename checks. A read-only recovery capture after 0097 confirms the preserved
8,029-byte `com.apple.MobileGestalt.b2l-before-profile-v2.plist`; the current
cache is absent because startup has not reached native regeneration.

Patch 0059 captures the typed critical-process exit reason before the existing
panic path. A bounded first-writer record prints pid, namespace, code, signal,
status and description without replaying panic varargs. Native critical exit
behavior remains intact. The owner's screen now identifies pid 1, namespace 7,
code 1, signal 6 and `No service cache`.

## Installation and validation

The production kernel compiled. Actual profile-transition and critical-exit
code passed ASan/UBSan tests. Scalar code-generation checks passed and the
kernel RCpc audit found zero unsupported instructions. The firmware and loader
remain byte-identical to 0095.

Recovery product/root/partition/capacity checks, a full 57,671,680-byte BOOT
backup, the prior 0096 image prefix and exact readback were verified before
reboot. BOOT image: 24,814,640 bytes; SHA256:
`447586bb8a924290278a2d3ddec2c0713467952925f52a4433b6f14cb1d0f52d`.
Private artifacts and the flash receipt are under `out/0097`.

## Runtime result

0097 still fails before normal service startup. Its persistent kernel region
and bootstrap files were captured read-only; HFS header comparison was unchanged.
There are zero installed application bundles. Native cache-loader output says
its copy succeeded, but this does not establish launchd accepted the resulting
shared-memory cache. The screen supplies the later critical-exit reason.

The complete resumed native trace archive saved with 0096 belongs to the older
0095 boot. Neither 0096 nor 0097 reached a new logd trace session; do not label
that decoded archive as their userspace runtime. Build 0098 addresses the
parent/child shared-memory length mismatch causing this regression.
