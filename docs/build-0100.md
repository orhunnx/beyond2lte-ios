# Build 0100: supply UIKit's missing event-profile target

## Change

The loader now supplies root target-type="D94" alongside model="iPhone17,2".
This resource-selection value matches the original source image's device tree
and the existing D94.Default.plist / D94.Touch.plist files. Native
hw.targettype obtains the root property and UIKit selects those profiles.
See [0099 evidence](0099-userspace-findings.md) for the exact chain.

The device-tree parser regression checks the new property is present exactly
once at root, with the expected NUL-terminated D94 value. Its capacity,
topology and ABI tests passed under ASan/UBSan. The EFI loader and embedded
4MiB firmware compiled. Packaging verifies the newly built loader is embedded.
The kernel is byte-identical to 0099, SHA256
cb12119eb0c722a751b417c7a5d7f2b407f1d7887e496335f6dbc60a9e8eaf6c.

## Scope and remaining work

This addresses the incorrect EventTimingProfiles/.Default.plist selection.
It does not establish a fix for icon drawing or alert background rendering.
Their NULL colorspace/context failures remain under investigation. The native
application installer currently rejects HFS staging with code166; the failed
one-attempt receipt is retained, and this firmware change does not trigger a
new migration. Installing all source-image applications is still incomplete.

## Installation

Installed from root S10+ recovery after product, BOOT alias and capacity
verification, a full57,671,680-byte BOOT backup and previous0099 prefix check.
The25,896,280-byte image matches exact readback SHA256:
8fa0af6c3d76b56dd62331e1889bff24f8d866c5aa77398940989c8e4d713e05.
Reboot returned successfully. Private artifacts and receipt are under out/0100.
Fresh runtime must confirm hw.targettype=D94, selection of the D94 profile and
disappearance of the empty-target alert.
