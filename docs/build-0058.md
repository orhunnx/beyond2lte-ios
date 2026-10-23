# Build 0058: undo archive opening and native display discovery

Remove O_NOFOLLOW from the undo archive's vnode_open fmode while retaining
VNODE_LOOKUP_NOFOLLOW_ANY. The native vnode_open wrapper maps the latter to
O_NOFOLLOW_ANY, and vn_open_auth rejects combining those two fmode flags with
EINVAL before pathname lookup. NOFOLLOW_ANY already protects the final path
component as well as intermediate components. No symlink fallback is added.

Keep the existing root mount, regular file, root owner, 0600 mode, single link,
root identity, A/B headers, record hashes and fsync/readback checks unchanged.
An unavailable archive continues to retain SYSTEM records and disable reuse.

The focused VFS test executes the actual undo_open body against the pinned
native conflicting-flag check. It covers successful archive initialization and
reopen, symlink refusal, ownership/mode/mount/link rejection, open failures and
parent-directory synchronization failures under ASan/UBSan.

## Display discovery

Change only /arm-io/disp0 device_type from display to display-subsystem. Native
IOMobileFramebuffer counts display-subsystem/ext-display-subsystem DT children
before starting IOService discovery. The existing raw-data property encoding,
IOMobileFramebuffer superclass and external=false service property satisfy the
inspected discovery and primary-classification checks. Root /framebuffer keeps
its simple-framebuffer description and display type. Framebuffer geometry,
reservations and touch support are unchanged. The actual DeviceTree serializer
passes the updated contract and existing bounds/topology checks under sanitizers.

SpringBoard receives its CADisplay list from the CoreAnimation render server.
Fixing DT discovery does not by itself establish that backboardd survives or
that its display list becomes available.

## Launch-constraint diagnostics

Capture up to eight AMFI operation 0x66 inputs/results from the opted-in
backboardd diagnostics. Read the actual 64-byte wire argument and result code,
with a maximum 512-byte prefix of constraints no larger than 16 KiB. Copy faults
and truncation are explicit; the existing global raw-data budget applies.
The decoder preserves these records with kind=amfi_constraint. Actual diagnostic
C tests cover failed copies, bounds, budgets and unchanged user memory.
No AMFI policy is registered and no syscall/constraint result is changed.

A build and complete BOOT readback verify installation only. Hardware evidence
must show a usable undo archive and reclaimed SYSTEM cells before slot reuse
can be called working. Display discovery, render-server availability and GUI
presentation require separate runtime proof.
