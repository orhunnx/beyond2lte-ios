# Build0096: refresh native device profile and register the approved app bootstrap

## Evidence motivating the change

The0095 runtime capture completed the PosterBoard59→61 data migration, then
aborted on a missing `order` key from generic Collections.plist. The source
image supplies Collections~iphone.plist. The current MobileGestalt cache still
contained five iPod/iPod touch answers and an artwork dictionary with only
scale3. The published app bootstrap had not run: no process, output, lock or
receipt, and zero installed app bundles. See0095-userspace-findings.md.

## One-time native profile regeneration

Before launchd, only in the existing explicit B2L development bootstrap mode on
writable HFS, b2l_profile_cache.h exclusively renames the old MobileGestalt
cache to com.apple.MobileGestalt.b2l-before-profile-v2.plist in the same directory.
It syncs the old file first and the directory after the rename. It refuses
symlinks, non-regular/non-root-owned files, multiple links, empty or oversized
files and cross-directory operations. An existing backup prevents another
invalidation, preserving a later regenerated cache on subsequent boots. The
native MobileGestalt service must rebuild its own answers from the current
metadata; this code does not supply a hand-written answer dictionary.

The backup is a profile-transition marker as well as a preserved original inode.
An already absent cache is left to native regeneration. Lookup/rename/I-O errors
are logged and do not block the regular boot path.

## Native launch cache registration

Disassembly confirmed that all three System/AppleInternal library prefixes are
protected by launchd's service cache. Moving a plist between them alone is
insufficient. The build reads the original launch cache from the local prepared
IPSW HFS image, adds exactly the approved owned job at
/System/Library/LaunchDaemons/dev.b2l.system-app-bootstrap-v1.plist and serializes
a separate binary plist. Removing that single entry must reproduce the original
parsed object exactly. The original cache SHA256 also matches the0095 live copy.

The owned executable is unchanged:72,480 bytes, SHA256
7537e9b516e6f0a21a817896a8162ffa93b1e51303affb0f3b606d3b85397176.
Its entitlements, native migration calls, one-attempt receipt and600-second
limit are retained. It does not retry a failed migration on every boot.

The publisher verifies the executable, new System/Library job and separate
/private/var/db/b2l-launchd-bootstrap-v1.plist, using synced resumable staging
and exclusive hard-link publication. The cache is1,198,735 bytes; the bounded
publisher maximum is4MiB, while the executable build limit remains512KiB.
Existing final files must match exactly and are never overwritten.

Only after all payloads verify and sync does an in-kernel readiness flag become
true. Patch0058 appends the native loader's documented two file arguments only
for the exact /usr/libexec/launchd_cache_loader path with argc1,64-bit pointers
and explicit bootstrap mode. It selects the separate cache and the original
signature path. Existing launchd_unsecure_cache=1 development behavior is
retained; no additional signature/entitlement policy is introduced. If
publication fails, readiness remains false and the normal original cache loader
arguments are preserved. Original Apple cache and job files remain unchanged.

The generated private header contains source-image cache data and stays under
ignored out/. Public sources contain only the generation/selection code.
The older unregistered /Library job remains inactive.

## Validation and installation

Actual profile-transition code passed ASan/UBSan one-shot, original-inode,
metadata, symlink and I/O-failure tests. Actual native-loader argument code and
XNU string copier passed exact-path, opt-in, readiness, argc/pointer-size,
space/error and accounting tests. Existing writer/persona/graphics-environment
tests passed. Production XNU compiled and linked. Scalar native-cache codegen
checks passed; the kernel RCpc audit found zero unsupported RCpc instructions.
The existing0095 firmware and loader are byte-identical in0096.

Only S10+ BOOT was written from recovery after root/product/partition/capacity
verification. The full57,671,680-byte BOOT backup matches the installed0095
prefix. Image size24,814,600 bytes. Exact BOOT readback SHA256:
edf35876eef4d8a2e4bda65b3f18bb96c031a4635f8e23490c764ee02de3c245.
The reboot command succeeded. Symbols, backup, audits, payload metadata and
flash receipt are private in out/0096. A5 was not operated on.

## Runtime still required

Installation is verified; UI/app migration success is not yet established.
The next capture should show B2L PROFILE refresh error=0, B2L APPS publication
error=0 and the native-cache-loader selection line, a rebuilt complete artwork
profile, the bootstrap attempt/result, installed app bundles and disappearance
of the wallpaper-order abort. This build does not directly patch the SpringBoard
assertion or replace wallpaper plists. Those failures must be checked against
fresh runtime evidence after native profile regeneration and installation.

## Subsequent runtime finding

0096 did not reach normal service startup. Its profile-cache owner guard rejected
the native uid 501 cache; 0097 corrects that guard. The derived launch cache was
shorter than the parent's fixed logical shared-memory view, producing the
`No service cache` critical exit. See [0097](build-0097.md) and
[0098](build-0098.md) for the runtime evidence and handoff correction.
