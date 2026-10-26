# 0089: current-format HID collection headers

## Evidence

0088 reads touch coordinates and matches the built-in digitizer in backboardd,
but the native decoder reports an invalid child event size at index1.
The prior0083 fix changed digitizer data from legacy104 bytes to native160
bytes, while retaining the old flattened collection framing. Individual data
codec tests passed; they did not exercise a complete parent-plus-finger packet.

The shipped IPSW IOKit code confirms two wire paths.
IOHIDEventCreateWithBytes at0x197f0f738 enters its helper at0x197eb3ff4 with
legacy flag1. The current internal path uses flag0: on EVERY event, the helper
loads attributeLength from a28-byte queue header and then advances to its data.
The current native writer at0x197eb9ba0 recursively emits each child's full
header; the legacy path recurses with data only. Mixing160-byte current data
with legacy framing makes the current reader interpret part of a child's
coordinates as framing. Kernel-provided current-format packets need the
per-child headers. These are local binary observations, not imported code.

## Change and verification

The B2L writer now counts and emits one28-byte header per event. Each subtree
gets its own eventCount, and weak parent links establish wire depth. The
current-format decoder consumes the matching headers and rejects malformed
lengths, attributes, depths, count mismatches and incomplete collections.
Single keyboard packets retain their previous bytes.

The new host test executes the actual production length/writer/reader methods.
An independent native-style header walker accepts the376-byte parent/finger
packet and564-byte nested packet, and rejects the former348-byte packet. It
also checks canaries, subtree counts, hierarchy, complete roundtrips, truncated
input, overflowing attributes, invalid depths and unchanged keyboard framing.
The existing160-byte digitizer codec tests pass. Real kernel compilation and
the linked RCpc audit are required before BOOT-only installation.

This fixes a concrete touch decoding defect. End-to-end swipe/tap acceptance
and startup speed require device evidence. The hours-long initialization and
other service crash families are not claimed fixed by this change.

## Installed

The kernel built successfully and its RCpc audit is zero. Only BOOT was written
after product/recovery/capacity checks, a full57,671,680-byte backup and exact
0088 prefix validation. The23,535,088-byte image and readback both have SHA256:

`fff9847142fbafeb036131d9d10081fcd8fd1ce4f515e9dcef20db462d17d5b2`

The reboot command completed. Physical touch/swipe acceptance remains pending.
Personalized images, symbols and receipts stay in ignored out/0089.
