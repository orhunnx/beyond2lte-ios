# 0101 HFS installation and unprofiled RGBA compatibility

Status: source and offline tests prepared; runtime activation is pending explicit
approval after automatic approval review rejected privileged process injection.
Nothing from this change has been flashed or enabled on the device.

## Proposed concrete activation

Publish two owned, versioned libraries under `/usr/local/libexec`, using the
existing root-owned, no-follow, sync-before-publication mechanism. Load them
through an exact-path `DYLD_INSERT_LIBRARIES` addition only after successful
payload publication. Both libraries independently require B2L development boot
arguments, `hw.machine=iPhone17,2`, and original build `23G90`.

| Library | Exact executable targets | Behavior |
|---|---|---|
| `b2l-hfs-install-compat-v1.dylib` | `/usr/libexec/installd`; `/System/Library/PrivateFrameworks/MobileInstallation.framework/XPCServices/com.apple.MobileInstallationHelperService.xpc/com.apple.MobileInstallationHelperService` | For the root HFS volume, route staging through native volume UUID/mount-point handling; choose native copyfile mode instead of clonefile mode. |
| `b2l-graphics-compat-v1.dylib` | `/System/Library/CoreServices/SpringBoard.app/SpringBoard`; `/usr/libexec/backboardd`; `/Applications/Preferences.app/Preferences`; `/System/Library/PrivateFrameworks/IconServices.framework/Support/iconservicesagent` | Supply sRGB only for unprofiled 8-bit, 32-bit RGB+alpha image/bitmap creation. Keep native allocation, drawing, release callbacks and error returns. |

The approved installation would also version the existing owned migration
launcher/job to v3 and its derived launch cache to v4. A new
`b2l-system-app-bootstrap-hfs-v1.plist` trial receipt would permit one bounded
native migration trial while preserving the previous failed receipt. No
capability, eligibility, consent, signature or trust-cache decision is overridden.
The original Apple service definitions and cache entries remain unchanged apart
from the existing single owned job addition. Shared launch-cache size stays exact.

This is persistent code running inside privileged services and UI processes. A
bug can crash installation or the UI, and native installation can modify app
containers. It requires explicit approval of this mechanism, not just the desired
outcome. Rollback is the verified 0100 BOOT backup: without the new exec environment
addition the owned libraries and versioned migration trial are not selected.

## Evidence and implementation

- Original 23G90 MI helper: `stagingLocationForURL:...` rejects non-APFS before
  native `_stagingLocationForVolumeUUID:...` (`0x1000032d8` guard).
- InstalledContentLibrary: volume UUID uses `getattrlist`; mode 2 uses clonefile
  with EXDEV-only copy fallback, mode 1 already uses native copyfile. The adapter
  selects mode 1 for HFS, retaining native traversal/metadata/symlink handling.
- CoreGraphics `verify_image_parameters` emits its NULL-colorspace message for
  both NULL and some rejected non-NULL spaces. This fix intentionally addresses
  only NULL, known RGBA8 layouts; it does not label every rendering error solved.
- Source: `userspace/hfs-install-compat.m`, `userspace/graphics-compat.m`,
  `userspace/b2l-runtime-mode.h`.
- `tests/test-runtime-compat.py` executes the actual adapters under ASan/UBSan.
  Quartz pixel contents, grayscale/alpha-mask preservation, callback lifetime,
  overflow rejection, HFS volume dispatch, APFS/foreign-volume delegation and
  NSError propagation passed on the host.
- Both libraries compile for the iOS arm64e ABI with warnings treated as errors.

Host tests do not prove iOS dyld insertion, native app installation completion,
icons, popup backing or wallpaper. After approval, build 0101, verify its BOOT
backup/readback before reboot, and collect a fresh runtime log for those outcomes.
