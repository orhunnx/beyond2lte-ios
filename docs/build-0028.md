# Build 0028: A5-style development policy integration

## Scope

This build keeps the existing ARM64e/PAN/pager implementation and adds the two
policy layers identified by the [shim comparison](shim-audit.md). It does not
change RAM reservations, firmware, root filesystem contents or device drivers.

- Patch 0016 registers the `Sandbox` MAC policy before userspace starts. Its
  unconfined defaults follow A5, with ENOENT for operations 4/5/6 and the verified
  iOS 26 zero-output response for operation 0x43. `PRIV_WORK_INTERVAL` is granted;
  other privilege grants remain denied by this provider.
- Patch 0017 registers a development AMFI interface and credential-label policy.
  It reads bounded XML metadata from the new main executable, exposes Boolean,
  string/array and object queries, and supplies XML to csops under existing
  access checks. Native credential references own the immutable objects through
  fork, exec, credential replacement and exit.
- The selected root filesystem's regular executables receive explicit
  development platform classification. The main-image decision feeds the native
  task/VM platform path before runtime mitigation setup; process queries also
  consult credential metadata. Native CLEARPLATFORM remains effective. Non-root
  filesystem exec clears inherited metadata and platform classification.

See [Sandbox policy](sandbox-policy.md) and [AMFI metadata](development-amfi.md)
for contracts and reproducible tests.

Both layers require ARM64e compatibility enabled and global signing enforcement
disabled. Existing real providers take precedence. The AMFI metadata is
**unauthenticated development input**: no verified cs_blob is created and no
CS_SIGNED/CS_VALID or page-hash validity is asserted. DER/CoreEntitlements opaque
queries and trust caches remain unavailable. This differs deliberately from the
older A5 signed-page path because native 16 KiB signature validation on this
4 KiB kernel has not been implemented.

## Previous device result and expected test

The 0027 screen demonstrates actual launchd startup logging after the container
query. The next failure is a shared errno assertion at launchd offset 0x57474.
A startup hostname write has a matching static error path; native XNU denies it
without platform status or the required entitlement. This build supplies that
platform contract. Only a new device result can establish that the observed
assertion is resolved.

Check for AMFI metadata interface/policy and Sandbox registration messages, then
launchd continuation beyond its boot-argument messages. A new error should be
reported with its complete final screen. The earlier preboot lookup error is
separate and remains unresolved.

## Validation and installation boundary

The complete seventeen-patch sequence supports an existing 0015/0016 tree,
repeat preparation and refusal of unrelated edits. Actual-source policy,
metadata parser/object/lifetime and native compilation checks accompany the
full standalone kernel build. Package verification checks the external firmware
and embedded loader plus the new kernel trailer. Local kernel/dSYM UUIDs are
matched and preserved with ignored build artifacts.

Installation targets only the previously authorized BOOT partition, with a
full-image-prefix SHA-256 readback. No new backup or USERDATA/SYSTEM/GPT rewrite
is required. Runtime launchd/service/GUI success remains unverified until a
subsequent device observation.

## Completed build and installation

The full kernel build, matching kernel/unstripped/dSYM UUID checks, host
policy/metadata tests, seventeen-patch upgrade/idempotence test, loader image
inspection and BOOT package validation passed. The 23,093,672-byte BOOT image
was installed, its complete image prefix was read back with matching SHA-256,
and reboot was requested. Device startup beyond the previous assertion still
requires the next screen observation.
