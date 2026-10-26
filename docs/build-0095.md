# Build0095: explicit unencrypted HFS and artwork profile

## Implemented and packaged

The shipped CoreServicesInternal createFileProtectionValue routine accepts only
classes1..4; it returns a null URL property for class0. PosterFoundation's
PFPosterPathURLResourceValues requires readable data and FileProtectionNone.
This explains a concrete mismatch with the old B2L HFS class0 query, even though
its files are explicitly unencrypted. Runtime removal of the PosterBoard59→61
failure still requires a fresh capture.

HFS staging revision2 reports PROTECTION_CLASS_D (4/None) only in the B2L build
without key services. An early setter accepts only the already-true None state.
Encrypted/unknown class requests fail ENOTSUP before other setattr mutations.
It does not write fictitious protection metadata, add key services or change
native CONFIG_PROTECT/non-board behavior. The source-provider hash manifest is
updated through the existing pinned staging mechanism.

The generated /product now supplies artwork description, phone idiom,
subtype2868 and scale3 from the original d94ap asset profile. The CPU BGRA path
uses sRGB and the source profile's conservative GLES2,0 artwork fallback rather
than an Apple Metal family. These tags select artwork resources; no GPU or
OpenGL service is registered. Physical framebuffer geometry remains1440x3040.

## Approved native application bootstrap

The owner explicitly approved the concrete scope: only in B2L development
mode, create the owned /usr/local/libexec/b2l-system-app-bootstrap-v1 and its
LaunchDaemon, invoke only the shipped SystemAppMigrator with minimum installation
entitlements, preserve existing files, and do not retry a failed migration on
each boot. The earlier automatic-review rejection was resolved by that explicit
approval; it is not an outstanding gate.

The loader opts in with b2l_userspace_bootstrap=1. After native HFS journal replay,
the publisher requires active B2L development policy and a writable HFS root.
It refuses symlinks, other mounts, unsafe parent permissions, mismatched files
or unexpected ownership/modes. It verifies any staging prefix, writes the
remainder, fsyncs and reads it back, then publishes an exclusive hard link. The
executable is complete before the LaunchDaemon is published. Directory syncs
follow publication. No existing final file is replaced. These files are new;
the existing persona manifest handling remains separate.

The owned arm64 launcher separately verifies the exact development/compatibility/
enforcement boot tokens, root, model iPhone17,2 and build23G90. It loads the
shipped bundle, checks MISystemAppMigrator and the zero-argument BOOL method ABI,
and invokes only performMigration. Native capability, eligibility, consent and
trust decisions remain native. A single-attempt receipt prevents an incomplete
migration from being automatically retried. Actual installed bundles and a
successful native return are required for its completion marker. The process
has a600-second alarm; launchd does not respawn it on failure. No general AMFI
policy or system-wide entitlement behavior is added.

The ad-hoc-signed helper is72,480 bytes; SHA256:
7537e9b516e6f0a21a817896a8162ffa93b1e51303affb0f3b606d3b85397176
An unchanged source rebuild produces the same bytes, allowing exact existing-
file verification on subsequent boots. Source and entitlement list are in
userspace/; scripts/build-userspace-bootstrap.py generates the private build
payload during source preparation.

## Checks and artifact state

The actual serialized device-tree output passes ASan/UBSan capacity/topology
checks. Actual staged HFS getter code passes all board/key-service modes; the
actual early setter accepts only4, rejects0..3/5..15 and leaves inactive requests
unchanged. Production firmware and XNU compile/link. Public modified sources
were checked for the private WiFi address; it is absent. Existing graphics
backtrace/performance/SMP changes are retained.

Private image: out/0095/boot.img. SHA256:
e9813c9e5607a59c7770f820f6474ba1bd001b84e6d25f69d20b2e7a07df25cd

The actual payload writer passes ASan/UBSan interrupted-write/prefix-resume,
corruption, owner/mode/link/type/length and I/O-error checks. The actual user-space
boot-token parser rejects lookalikes and disabled options. Persona seed tests
still pass. Scalar machine-code audits pass and the linked kernel has zero RCpc
instructions.

## Installed

Recovery/product/root/BOOT alias and capacity were verified. A full57,671,680-byte
BOOT backup was taken and its0094 prefix matched exactly. Only BOOT was written
from recovery:23,618,240 bytes. Exact readback matches the image SHA256 above.
The ADB reboot command completed successfully. Image, symbols, backup, audits
and flash receipt remain private under out/0095.

A fresh run must demonstrate publication error=0, the B2L APPS native migration
result, installed application bundles, PosterBoard data-store initialization,
and the visible wallpaper/Settings behavior. Exact BOOT installation is not
proof that these user-space issues are resolved. During the next iOS boot, the
approved publisher/native installer can write USERDATA through native HFS VFS;
no raw USERDATA/SYSTEM partition was flashed from recovery in this operation.
