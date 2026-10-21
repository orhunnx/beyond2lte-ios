# Build 0013: native HFS root and SYSTEM write support

## Starting point

Build 0012 physically verifies native UFS capacity, read-only BSD devices,
primary/backup GPT CRCs and SYSTEM content reads. The subsequent authorized
empty HFS+ SYSTEM image has been written and fully readback-verified. The
original Android BOOT and SYSTEM backups remain intact; no new backups are
required.

## Integration

This build links the pinned, open-source HFS filesystem provider and selects
the unique validated HFS SYSTEM partition through `rd=b2l-system`. The loader
explicitly requests `b2l_root_rw=1` and `b2l_unsealed_root=1` for the custom,
unsealed development root. This policy does not claim Apple root authentication.

The native transport adds real WRITE(10)/(16) commands and non-IMMED SYNCHRONIZE
CACHE(10). Each write completes a real cache flush before reporting success.
The original calibrated UFS link and physical RAM/MMIO guards stay unchanged.
The BSD layer restricts writes to the uniquely selected HFS SYSTEM extent;
the whole LU, GPT, BOOT and other partition views remain read-only. Bounds are
checked before I/O, including requests requiring partial physical-block updates.

An empty HFS volume has no init or iOS userspace. A later missing-init failure
is distinct from successful root mounting and storage writes.

## Verification boundary

Compilation and host checks establish neither a physical write nor an HFS
mount on the phone. The device must report the selected SYSTEM root and an
actual read/write HFS mount. Any reported write/flush error remains a failed
operation; unsupported controls do not return fake success.

The firmware profile contains the newly built loader with the explicit root
arguments. The provider and root-policy details are documented in
[HFS provider](hfs-provider.md). The empty SYSTEM image preparation is documented
[separately](system-hfs.md).

## Host build and checks

The standalone kernel and dSYM, EFI application and 4 MiB firmware profile build
successfully. The complete pinned HFS provider is linked, including the actual
MD5 volume-UUID dependency. Crypto fallbacks do not install a partial global
provider table, and the MD5 implementation is compiled with general registers
only. AES regression and RFC/incremental MD5 tests pass.

ASan/UBSan checks execute the real UFS protocol core, bounded disk read/write
functions, strategy/raw I/O and controls. They check SYSTEM-only authorization,
whole-LU alias rejection, cross-boundary refusal before I/O, partial-block RMW,
errors/residuals, real flush errors and multi-iovec copy-fault handling. HFS tests
execute real registration/mount-policy code and the actual native alias branch.
An independent review caught and corrected raw writev advancement against XNU's
actual `uio_update` behavior before installation.

Patch preparation verifies the entire ordered patch state against pinned source,
applies only the missing suffix, and refuses local changes. This preserves
idempotence when later patches modify context introduced by earlier patches.
Pristine application, repeated application and edited-input refusal were checked.
The reference firmware physical guard map remains byte-identical.

The final kernel is 18,479,952 bytes and the validated BOOT image is
23,053,456 bytes, within the confirmed target BOOT capacity.

## Authorized installation

The final BOOT-only installation completed. Target identity and capacity were
rechecked; the exact image prefix read back from BOOT matched the packaged
image, and reboot was requested. The original Android backups are retained and
no additional backups were taken. The previously verified empty HFS+ SYSTEM
image remains the root candidate. Raw receipts and identifiers stay ignored.

Device proof of the HFS mount and native write/cache flush remains pending.
The expected success lines are `B2L HFS: native root mounted read-write` and
`B2L UFS: first native write and cache flush completed`. A later failure to find
init on this deliberately empty filesystem is a separate expected boundary.
