# Empty SYSTEM HFS+ preparation

The build 0012 device photograph verifies native UFS capacity, read-only BSD
nodes, primary/backup GPT CRCs and the original Android LP SYSTEM signature.
This is physical storage-read proof after ExitBootServices, independently of
filesystem integration.

After explicit authorization, the original Android SYSTEM/super container was
replaced with an empty, non-journaled HFS+ volume named `B2LIOSROOT`. The original
Android container backup had already been verified against the device. Its full
length and stored digest were checked again locally before the write; no new
BOOT or SYSTEM backup was taken.

The image contains a filesystem directly, with no nested partition table or
UDIF trailer. Its length exactly matches the existing physical SYSTEM extent.
It was created with `hdiutil create -sectors <partition-sector-count> -layout
NONE -fs HFS+ -volname B2LIOSROOT -type UDTO <local-output>`. Its primary and
alternate volume headers agree, allocation geometry accounts for the full
extent, the clean-unmount flag is set, and journaling is disabled. A read-only
`fsck_hfs -fn` check reported the volume healthy.

Recovery identity, root access, both SYSTEM/super aliases, exact capacity and
unmounted state were checked before writing. The complete raw image was streamed
to SYSTEM without staging the large file in recovery tmpfs. After `sync`, a
full-partition device digest matched the local image. BOOT, GPT and all other
physical partitions were left unchanged. Images, raw layouts, backup hashes,
identifiers and installation receipts remain local under ignored `out/`.

This empty volume contains no Apple binaries, init process or iOS userspace.
Build 0012 can recognize its HFS signature but has no HFS filesystem provider
or direct native root selector. Installing the image does not establish a root
mount; HFS registration, root selection and actual read/write integration are
separate kernel work.

## Subsequent correction: journaling is required

This document records the initial non-journaled image, not the current preferred
format. After the first writable mount ended in a separate kernel panic, the
next boot refused that dirty non-journaled volume. The current SYSTEM has been
repaired and converted to real Journaled HFS+; see [journal repair](system-journal.md).
Future writable root images must use `-fs 'Journaled HFS+'`.
