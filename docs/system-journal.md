# SYSTEM HFS journal repair and enablement

## Observed failure

The original empty HFS+ image was non-journaled. Build 0013 mounted it read/write
and completed real native writes before a separate AMFI panic. The next boot
therefore encountered a dirty volume. Build 0014 reported
`hfs_mountHfsPlus: cannot mount dirty non-journaled volumes` and refused the root.
A non-journaled writable root was unsuitable for these panic/reboot trials.

## Repair and real journal creation

Recovery identity, root access, the actual SYSTEM extent/capacity and unmounted
state were verified. The current SYSTEM contents were read into a local working
image; its complete digest matched the device. The existing original Android
backup remains unchanged, and no new Android BOOT/SYSTEM backup was taken.

macOS `diskutil repairVolume` ran real `fsck_hfs -fy` on the attached working
image. It repaired a volume-directory-count inconsistency, rechecked the volume
and returned filesystem check status zero. `diskutil enableJournal` then created
the actual internal journal structures. Merely changing the volume's journaled
attribute or forcing the dirty volume to mount was not used.

After clean detach, the working image passed read-only `fsck_hfs -fn`, reported
as a healthy Journaled HFS Plus volume. On-disk checks verified:

- HFS+ signature and full-volume allocation geometry;
- journaled and clean-unmount attributes on the primary header;
- agreement with the alternate header, apart from macOS's primary-only final
  clean-unmount bit update;
- an internal JournalInfoBlock with an entirely in-volume 8 MiB journal;
- journal magic, endian marker, bounds and empty transaction queue;
- the actual journal-header checksum and 512-byte journal header size.

## Bounded writeback

A byte comparison against the exact captured source identified seven changed
4 KiB-aligned extents, totaling 229,376 bytes (224 KiB). The device's full SYSTEM
digest was rechecked before the first write. Each extent was bounded to SYSTEM,
its transferred bytes were checked, and completed writes were synchronized.
Journal/catalog data was written before the alternate and primary volume headers.
The final full-partition digest matched the repaired journaled image.

Only those changed extents were transferred back to SYSTEM. BOOT, GPT and other
partitions were unchanged. The phone was rebooted using the existing build 0014.
Working images, raw offsets, digests, chunks and receipts stay local and ignored.

## Kernel support and evidence boundary

The existing OSS HFS provider includes internal journal creation/open/replay.
Its native BSD backend supports the required writes and real cache flushing;
no FUA, barrier, discard or external-journal service is falsely advertised. The
512-byte journal header is supported by the backend's sector-size translation.
No journal-disable boot argument is supplied. No new kernel build was needed.

The repair, actual journal structures and device writeback are verified.
Journaled root mounting and later recovery after a device panic remain physical
device tests. The empty root still contains no init or iOS userspace.

Future empty root images must use `-fs 'Journaled HFS+'` with `-layout NONE`
and exact partition geometry. A previously dirty filesystem must first pass
real repair; clearing its dirty flag or bypassing HFS checks is not a repair.
