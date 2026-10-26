# 0084: bounded deferred touch initialization retries

## 0083 device evidence

The owner reports that SpringBoard finally appears with fluid animations, while
finger input has no response. The recovered kernel log shows successful EFI
sensor setup, followed by a failed first XNU device-ID read (`cmd=0x22`). The
100,000 us probe budget expired after 768,702 us with five receive bytes in the
FIFO. The reason for the elapsed-time jump is not yet established. The one-shot
startup path left touch disabled for the rest of the roughly 5,974-second log:
`enabled=0`, `wire=0`, `updates=0`. GPIO power-button events still reached HID.

The completed read-only export contains 928 files. Four current-boot trace files
decode to 506,656 events with the same boot UUID. backboardd explicitly attaches
`B2LHIDTouch` as a matched, built-in digitizer; the missing finger input is before
that attached HID service. Current exported crash reports contain no SpringBoard,
backboardd or assetsd report. The decoded current-boot records contain no
DataMigrator events and 542 sharingd events, unlike the previous measured retry
storm; missing records and decoder string warnings prevent claiming complete
coverage or that every related service problem is resolved.
This run cannot validate the 0083 digitizer wire fix because no sensor events
reached its serialization path.

## Change

Keep the existing device-ID, integrity, boot-status, event-clear, touch-functions
and sense-on sequence. If startup fails with inherited HSI2C timings present,
retry it on the input workloop after one second, with four attempts maximum
including the initial attempt. Only a fully successful sequence enables touch.
Missing timings do not schedule retries. Restore the normal transport timeout
after every attempt, and report attempt/pending counts in persistent logs.

GPIO polling remains active. The existing three-consecutive-runtime-transfer
failure cutoff stays unchanged. There is no firmware update, voltage change,
power cycle, frequency change or removal of bus deadlines.

## Verification and limits

ASan/UBSan tests execute the production initialization body, covering recovery
from a failed first probe, the four-attempt cap, missing timings, failures at
every read/write stage, and invalid integrity/boot status. The production poll
body tests deferred scheduling, event ordering, bounded FIFO bulk reads, partial
read failures, GPIO cadence and the runtime cutoff. Existing MMIO transport
budget tests pass. Kernel build and zero-RCpc audit pass; EFI and loader are
reused from 0083.

Only BOOT was updated after a full 57,671,680-byte backup and verification of
the installed 0083 prefix. The 23,465,656-byte image and its readback both hash
to `0025f3870a557dc0d3a5ae8e602ae08ca8cd5f15440ff2fb85348ca8c56ccd0e`;
the device was then rebooted. The receipt is `out/0084/flash-receipt.json`. Device touch response
and the native digitizer ABI still require a new physical run.

## Recovered device result

The next recovery read captures epoch46, with 1,991 valid console pages,
6 missing byte positions and 6 producer drops. It runs through uptime3,216s.
The first device-ID attempt times out at766,586us; attempt2 completes the
full native setup (`boot=0x20`, `integrity=0x80`) and clears retry_pending.
The final input sample at3,214s has touch enabled, 18,590 wire records,
6,042 parser updates and33 cumulative transfer failures. backboardd's native
log attaches B2LHIDTouch and already reports249 events at attachment. This
verifies sensor recovery and delivery into backboardd, not responsive UI input.

The first physical IOSurface copy is bracketed by1,301s and1,311s; 0083 was
bracketed by1,281s and1,291s. These are first presentation times, not proof of
home-screen readiness. The owner still sees the Apple logo after50minutes.
The final cumulative bus duration is311.767106s, about9.7% of elapsed uptime;
it includes time spent preempted and is not a measured CPU utilization ratio.
Most idle callbacks are about2.2ms. An active32-record callback can be about
46ms. The three additional initialization retries cannot explain a persistent
50minute delay; only one additional attempt actually ran.

A complete read-only observed HFS export contains943 files with a stable source
header. Two current-boot trace files decode to172,839 events. Their newest event
is230.064s, far earlier than the console end at3,216s. Dirty HFS metadata is not
journal-replayed by this export, and UUIDText decoder warnings remain. The gap
does not establish that logd stopped; later persisted crash reports exist.
No conclusion about the final50minutes may rely on these early native messages.
AccessibilityUIServer launch watchdog reports show severe system CPU usage in
several measured30second windows (for example237.450s system versus4.040s user
across all CPUs). This is evidence of kernel-side load in those windows, not
an attribution to the scheduler, emulator, storage or rendering alone.

The CPU-capacity limitation is confirmed in source: all8 DT CPU nodes advertise
cluster-id0 and cluster-typeS, B2LCPU requires that single cluster, and B2L.h
sets NO_ECORE. There is no B2L DVFS policy. Thus the scheduler has no three-tier
M4>A75>A55 capacity model. Recovery reports separate Linux policies for CPUs
0-3,4-5 and6-7; their recovery frequencies do not measure their frequencies
while XNU was running. Changing affinity alone cannot establish the cause of
the logo wait or provide a validated performance fix.

The last emulation counter is44,525,264 traps. Cache counters still show four
workers and an aggregate0xff CPU mask, with no reported disk errors. That mask
proves that workers visited all8 CPUs, not good load balancing or CPU speed.
The last cache sample has pending124/active3; lack of a later worker-owned
report is not by itself proof of a cache deadlock.
