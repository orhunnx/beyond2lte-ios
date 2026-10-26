# 0100 runtime and the owned bootstrap/native-cache interaction

## Read-only capture

Recovery product/root identity was checked. The persistent kernel region is
epoch61, with1,485 valid records,353 missing positions and43 producer drops.
A complete native/supplemental export reports errors=[] and unchanged HFS
header. Boot8B7FB045-C87B-46EC-B07B-4F6731DC0C34 yields333,189 decoded events.
Late unflushed records can still be missing. Raw archives, device metadata and
images remain private in ignored out/.

## D94 profile selection works

SpringBoard, PosterBoard, Preferences-related services and other UIKit clients
now log Event Timing Profile for Touch: ok, path=
/System/Library/EventTimingProfiles/D94.Touch.plist. No Default Event Timing
warning is present in the decoded boot. D94.Pencil.plist is still absent, as it
is in the original source image; there is no Pencil device to configure.
This confirms progress beyond the empty-target resource-selection failure.

## Rendering and SpringBoard remain incomplete

The capture contains48 invalid image colorspace: NULL messages and328 invalid
context messages. These counts cover a different duration from0099 and do not
by themselves establish a rate regression. The kernel records SpringBoard's
Invalid condition not satisfying: _bs_assert_object != nil again. The owner
has not supplied a successful sustained icon/backdrop rendering checkpoint.
Framebuffer copies continue with no sampled unsupported-format/bounds failures.

## Why the bootstrap is not selected again

Kernel publication reports error5 (EIO); the cache loader uses the original
Apple cache paths. Current v2 helper bytes were read through the HFS catalog
with hard-link resolution. Its size is unchanged at72,480 bytes. Exactly34
four-byte words differ from the built source executable; each is an
allowlisted observed-instruction-cache rewrite, for example RETAB to RET and
authenticated branches to their native branch forms. The selected v3 derived
launch cache is byte-identical to its generated source.

The publisher incorrectly required the existing helper to remain byte-identical
after the deliberately enabled native cache had translated it. That is an
idempotence bug in our publisher, separate from the installer's HFS rejection.
The migration log and attempted=true receipt retained on disk are the prior
0099 attempt. They are not a new0100 install trial. Zero app bundles are installed.

Build0101 corrects the publisher comparison. It keeps exact comparison for
metadata, data, headers, job/cache payloads and staged writes. For the one
owned existing executable, it permits only original-to-native words accepted
by the production b2l_nc_native converter and located in executable Mach-O
sections explicitly marked pure instructions. Invalid/overlapping command or
section geometry, unknown conversions and other changed bytes reject.

The actual-code test accepts all34 changes from the saved phone executable,
rejects the same file in strict mode, and checks malformed metadata, non-code
changes, bounds and unknown conversions under ASan/UBSan. The generic resumable
writer test also passes. Production XNU compiles; native-cache scalar checks
pass and the linked image has zero unsupported RCpc instructions. It remains
uninstalled under private out/0101. Installed BOOT is0100.

This comparison fix does not clear the migration attempt receipt or retry a
failed install. HFS staging compatibility and native desired-state handling
remain outstanding; see hfs-apfs-installation.md.
