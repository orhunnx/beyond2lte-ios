# 0086: separate storage work from the shared system group

## Evidence and change

0085 booted all eight CPUs and all three native Edge clusters. However, its
A75 override selected THREAD_GROUP_SYSTEM, which XNU also assigns to launchd
and userspace services. Fresh SpringBoard/backboardd progress samples appeared
only on CPUs 0-5, with no first copied IOSurface frame during about 51.7 minutes.
The kernel log contains no panic.

Patch 0054 seeds the A75 preference only for THREAD_GROUP_IO_STORAGE (group 4).
The shared system group (group 0) now follows the normal QoS policy:

| Work | Initial preference |
| --- | --- |
| Foreground, initiated, default and fixed-priority buckets | Samsung M4 / pset 2 |
| Utility | Cortex-A75 / pset 1 |
| Background | Cortex-A55 / pset 0 |
| Explicit IO-storage group, except background | Cortex-A75 / pset 1 |

The four native-cache workers join the existing XNU IO-storage group at worker
entry. Native Edge priorities, spilling, migration and CPU binding rules remain
in effect. The change does not alter frequencies, voltage or durable cache data.
Progress samples now include the actual thread-group ID alongside CPU number.

## Verification

The host test executes the actual native thread-group initializer for groups
0, 1, 4 and 99 across all six buckets, verifying that system/default work chooses
M4 and storage work chooses A75. ASan/UBSan and the targeted progress snapshot
test pass. Production XNU builds successfully and the linked RCpc audit reports
zero instructions. The full board patch sequence is checked separately in the
private build log. Firmware and EFI loader are identical to 0085.

0085 recovery export contains 951 files with a stable header and a complete
receipt. Its native decoder produced 77,436 events; coverage and exported crash
reports must not be treated as proof of an entire crash-free runtime.

## Package and installation status

The private BOOT image is 23,532,696 bytes, SHA256:

`443e9f587d769c7e884367d1dfb800e31ff18b51eda9055d4083d08190b8a50a`

The first installation attempt stopped before any backup or write because the
S10+ disappeared from ADB. After reconnection, 0086 was written only to BOOT.
Recovery/product/capacity checks passed; the full 57,671,680-byte BOOT backup
matched the exact 0085 image prefix. The new image readback SHA256 matches the
package hash above, and the reboot command completed successfully. The private
receipt and rollback image are retained under out/0086.
Improved foreground placement, startup time and usable touch require a device
run; no performance improvement is claimed from host tests.


## Recovered device run

The owner reports no UI after about 40 minutes, with console text still moving.
The recovered kernel log reaches 2,443 seconds and verifies all eight CPUs,
three Edge psets and bidirectional SGI1/timers. Fresh SpringBoard observations
include CPUs 6 and 7 (16 of 43 samples); backboardd includes CPUs 6 and 7
(53 of 107). These are syscall-boundary samples, not utilization percentages.
M4 placement is therefore observed, while startup improvement is not.
No kernel panic or first copied IOSurface frame is present. The native-cache
summary has zero errors and no pending work; the last summary records 25.7M
compatibility traps. This count alone does not identify the latency cause.
The touch path remains enabled and records 1,882 updates.

All 961 native/supplemental files were exported with a stable volume header,
complete receipt and no export errors. Current-boot decoding yields 68,761
messages, but native trace time spans only roughly 38-126 seconds. It cannot
explain the whole 40-minute run. No current-boot IPS crash report was found;
this does not establish that every service remained healthy.

IPSW symbolication uses a shared-cache slide of 0x125c0000, independently
matching the observed mach_msg2_trap return address. SpringBoard's main thread
at second 387 is sampled in BKSSystemShellService initialization through
BKSDisplayServicesStart, BKSDisplayServicesGetMainScreenInfo, BKSServerPortHelper
and bootstrap_look_up. backboardd's main thread is still sampled in
CA::Render::Server::start at second 2,183 (dispatch source installation) and
2,269 (x_thread_create). These snapshots locate a long pre-frame initialization
path; they do not prove that either exact call consumed the intervening time
or identify a failed RPC or a renderer implementation defect.

The progress recorder's 16 slots per role never recycle. Dead/old thread
observations fill those slots, and overflow prevents newer threads from being
recorded. Thus SpringBoard's latest stored progress time at second 816 must
not be interpreted as its last actual execution. A definitive wait/runtime
attribution needs fresh thread observations or task/thread CPU-time sampling.
No additional BOOT was written during this log analysis.
