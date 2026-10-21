# Builtin unencrypted HFS provider

## Source and build inputs

The provider is Apple's actual [HFS 715.120.4 source](https://github.com/apple-oss-distributions/hfs/tree/hfs-715.120.4),
pinned to commit `124b970fd5390f1c2cd6f985478616a04f6eeb1f`.
It is the release-paired candidate for XNU 12377. The stager fetches that
immutable commit, verifies the source revision and unmodified source trees,
and records hashes of every staged file in `b2l-provider.json`. Reusing an
edited or unmanaged staging directory fails rather than overwriting it.

`scripts/stage-hfs.py` copies the upstream `core` and `hfs_encodings` directories
to the ignored local XNU tree under `bsd/b2l_hfs/upstream`. Upstream license and
copyright headers remain intact. Only the 37 core C translation units and the
small builtin registration/mount wrapper are compiled. The public SDK's
`TargetConditionals.h` is a local build input; no SDK/KDK headers or proprietary
filesystem binaries are committed.

Example using an existing clean pinned checkout:

```sh
HFS_SOURCE=/path/to/hfs-715.120.4 scripts/build-kernel.sh
```

The normal KDK/dependency/XNU/build-directory environment variables still apply.
`scripts/prepare-xnu.sh` applies the base board patch, then
`patches/0002-b2l-builtin-hfs-root.patch` and the MD5 prerequisite patch
`0003-b2l-hfs-md5.patch`, then copies the source overlays. The ordered patch
state is checked against pinned source before applying a missing suffix.
The separate HFS patch must not also be folded into the base patch.

## Configuration and registration

`b2l_hfs_compat.h` selects the kext KPI view of XNU headers, then uses upstream
HFS's iPhoneOS configuration. Compression, FIFO and secluded rename support
remain as upstream configures them. Content protection is explicitly disabled:
this initial volume is unencrypted. The staging transformation guards the
AppleKeyStore include and wrapping-key prototypes accordingly; it neither
emulates keys nor reports encrypted files as decryptable.

Legacy HFS Standard/MacRoman converter interfaces are excluded by the upstream
iPhoneOS configuration and are not compiled or called. HFS+/HFSX use their real
Unicode paths. Builtin registration copies the upstream HFS operation table,
registers the real vnode operations with `vfs_fsadd`, initializes HFS zones and
sysctls, and applies upstream CPU-dependent cached-origin tuning. It runs after
`vfsinit` and before root discovery. A registration failure remains fatal.

The wrapper only changes the root mount's initial read-only flag under the
conditions below. Filesystem allocation, B-trees, catalog, attributes, vnode
operations and journal code remain the pinned OSS implementation.

## Native root and development policy

`rd=b2l-system` selects the unique validated HFS SYSTEM device returned by the
native UFS BSD adapter. The log reports its actual canonical `b2lufs0pN` name.
An explicit `rd=b2lufs0pN` is accepted only when it equals that same validated
SYSTEM device. A missing device or a different partition request fails; a
hardcoded device major, GPT index or recovery alias is not substituted.

This route does not publish fake IOMedia or IOBlockStorageDriver objects.
When those optional classes do not exist, IOService records that no IOKit
storage service owns the native BSD root and has no media instances to unhide.
Existing behavior remains for configurations where the classes are present.

`b2l_root_rw=1` permits the HFS mount wrapper to clear `MNT_RDONLY` only for a
root mount whose device equals the adapter's validated SYSTEM device. The disk
adapter independently limits writes to that selected partition; whole-LU and
other partition nodes remain read-only. Omitting this flag retains an initial
read-only root. The mount log reports the actual resulting mount flag only
after the real HFS mount call succeeds.

`b2l_unsealed_root=1` is a separate development opt-in. When the authenticated
root ioctl fails, continuation requires this exact value, the same validated
SYSTEM device and an actual HFS root mount. The log states that the root is
unsealed and reports the authentication error. No authenticated-root ioctl,
signature verifier, key service or sealed-volume capability returns fake
success. Without this opt-in, the existing authentication failure remains
fatal.

## Limits and verification

### MD5 volume UUID prerequisite

HFS computes its volume UUID during mounting using the legacy MD5 API, including
a 16-byte namespace update followed by an 8-byte on-disk name update. The old
XNU adapter required the unavailable global crypto table. Patch
`0003-b2l-hfs-md5.patch` adds a real B2L fallback backed by the same pinned MIT
BearSSL checkout as AES; the existing real-provider path remains when that
provider is present. There is no partial global table or fabricated volume UUID.

The 88-byte XNU context and 96-byte BearSSL context are marshaled explicitly.
State words and the partial-block buffer remain copyable without embedded
pointers. The count preserves XNU 12377's actual adapter ABI: high word first,
total **bytes**, despite the older header comment describing a different count.
Static assertions pin the context size and count/buffer offsets. Both the new
implementation and legacy dispatch object compile with `-mgeneral-regs-only`.
MD5 is used here for legacy filesystem compatibility, not authentication.

The BearSSL staging manifest is format 2. Its explicit format-1 migration first
verifies every old staged hash, then adds only the pinned MD5/LE-codec files;
it rejects any migration that would alter an existing AES input. Edited or
unmanaged trees remain errors. `tests/test_md5_provider.py` exercises the actual
legacy dispatch and real BearSSL code under ASan/UBSan using RFC 1321 messages,
padding and block boundaries, incremental/HFS-shaped updates, context copies,
canaries and byte-counter carry/wrap. The AES NIST regression also passes.

### Remaining limits

External-journal IOMedia enumeration is unavailable; the builtin glue enumerates
no such devices. The fixed UFS device is not ejectable and exposes no platform
serial through HFS. Encryption, Apple sealed-root authentication and a full
IOStorageFamily stack are not provided. The earlier kernel crypto adapters do
not constitute a complete global crypto provider; unrelated algorithms retain
their documented limitations.

An empty HFS volume is not an iOS root. It has no init/launchd and cannot establish
userspace boot. Real HFS mounting, writes/synchronization and later missing-init
behavior must be distinguished in device evidence. No proprietary init program
is supplied by this integration and no success claim relies on bypassing init.
The 0013 device result confirms a read/write native HFS mount and completed
native writes/flushes; it then reached the separate missing AMFI entitlement
interface fault described in [unavailable AMFI handling](amfi-unavailable.md).
It does not establish init or iOS userspace execution.

All 37 upstream HFS core C files and the builtin wrapper compile against the
current B2L XNU headers. `tests/test_hfs_provider.py /path/to/prepared/xnu` uses
the actual registration and mount-wrapper source bodies and actual unsealed
policy source under ASan/UBSan. It checks vnode-operation registration, callback
preservation and initialization order; read-only defaults; device/boot-argument
restrictions; propagation of mount errors; and the native alias selection branch
from the prepared XNU source. These host checks do not replace a physical mount
or storage-write test.
