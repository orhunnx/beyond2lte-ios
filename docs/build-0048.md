# Build0048: four native-cache preparation workers

## Prerequisite

0047 has user-supplied device proof of all six per-CPU timers and bidirectional
SGI1/xcall checks (four A55 plus two A75). M4 cores remain excluded.

## Change

Four scheduler-managed workers now own separate page/record/verification
buffers. VM-to-vnode resolution, file identity and source-page reads can run
concurrently. A sleepable mutex serializes slot allocation, integrity metadata,
latest-generation merge, A/B write, flush, exact readback and RAM publication.
An I/O failure disables learning; every waiter rechecks this flag under the
commit mutex. The last valid copy is preserved. Original source files are
still applied only before userspace, and corecrypto measured-text exclusion
is retained. This does not attempt simultaneous UFS hardware transactions.

New accepted requests wake a worker. Wait registration precedes the pending
recheck, eliminating the unconditional one-second pause while backlog exists.
The queue remains bounded at127 outstanding entries and four in-flight jobs.
Default workers=4; b2l_cache_workers=1..4 can reduce it. There is no CPU affinity
pinning; the XNU scheduler places workers on the verified six-core topology.

Thirty-second statistics retain cumulative queued and add pending (actual ring
depth), active, peak, workers, visited CPU bitmask and per-worker completions.
These identify activity; CPU mask alone does not prove simultaneous execution.

## Verification and installation

The host test extracts the actual identity, learn and persist bodies, uses
four concurrent pthread workers with independent buffers, and checks same-page
merge, sequence retention, duplicate suppression, distinct-slot allocation,
full-cache rejection, measured-text exclusion and stop-on-write/flush/readback
failure. ASan/UBSan passes. This models VFS/I/O, not the XNU scheduler.
The incremental kernel build succeeds and scalar compiler flags and actual
machine code were verified. Matching kernel/unstripped/dSYM UUID: 548F9D30-D9EC-35E7-B102-6D1372A78761.

At the user's request, first read the current16MiB SYSTEM log tail: epoch13,
111 valid records,225689 text bytes, zero missing positions and producer drops.
The0047 log shows applied373 + already5088 =5461 occupied cache slots, errors0
and flush_errors0. Multiple userspace service crashes remain; this change
makes no claim to fix them or achieve SpringBoard/UI operation. Full capacity
continues to reject new source pages even with faster preparation.

Then only BOOT was updated after matching0047's complete prefix; full0048
readback matched `2bf65ad9665df342e3c66826c4e58cf0a3100a6be151b843510bea414a11d0f5` and reboot was requested.
Firmware/loader were reused. No recovery USERDATA writes were made. Known0039
and the device-proven0047 package remain available for rollback.
0048 runtime worker activity/performance remains to be observed on the device.
