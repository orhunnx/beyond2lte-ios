# Build 0035: detailed exception and exit logging

## Reason for the diagnostic run

The recovered 0034 epoch-7 log has 80 valid records and 116147 text bytes,
starting at sequence zero, without sequence gaps, missing positions or producer
drops. Some console lines interleave; the final persisted line is incomplete.
There is no saved early-panic message, which does not establish an empty RAM tail.

FIPS integrity now explicitly passes, followed by `all tests PASSED (22427 ms)`
and aggregate result true. Pre-init application reports 3426 applied pages,
1130 already matching and zero skipped. The measured corecrypto range remains
original: 589 excluded words and zero requiring restoration on this boot.
launchd reports early boot complete and continues system boot.

Observed crash messages include at least 11 SpringBoard, 14 usermanagerd,
4 securityd, 4 UserEventAgent and 2 watchdogd failures. The watchdog daemon
explicitly cannot discover its watchdog KEXT service. SpringBoard's exact exit
reason is absent: the original custom exit trace only records eight exits plus
PID 1, so early successful boot tasks consume the sample budget.

ReportCrash is attempted but no runtime crash report appears in the read-only
catalog scan. The expected private-var cores directory is absent and a watchdog
core creation attempt fails ENOENT. USERDATA is journaled but not marked clean;
it is not mounted/repaired/replayed in recovery, so pending metadata is a limit
on negative file-inventory conclusions. About 12.04 GiB remains free in the
32 GiB HFSX volume.

The current fixed cache has all 5461 slots occupied, 9527 CRC/SHA-valid copies,
zero invalid nonempty copies and zero duplicate source-page keys. It contains
42123 native word differences, including retained undo-record differences in
the excluded corecrypto region. There is no room for new pages, although
existing page records can be updated. Originals must not be evicted to make room.
The last sampled learner counters are 30767 queued, 10337 learned, 3426 applied,
9507 skipped, 379152 dropped and zero persistence errors.

An unhandled-or-fault report for a supported authenticated branch remains
unresolved; the saved line does not identify its process or failing validation
stage. Denied process-info/CPU-monitor privileges and incomplete graphics and
platform services are additional candidates, not confirmed SpringBoard causes.
177 parseable unnest events across 55 process names do not themselves establish
177 physical memory copies or a VM leak.

## Change and verification

Only the loader's command line gains `exception_log_max_pid=4096`. The pinned
DEVELOPMENT kernel already implements this tunable in exception.c and uses it
for exception delivery records and namespace/code/description records in
kern_exit.c. This avoids changing the runtime/emulator/cache behavior to add
initial diagnostics. System events can still be skipped with interrupts masked.

The compiled EFI image contains the exact parameter, the existing kernel
contains its tunable and diagnostic format strings, and the package verifies
the real embedded loader. Firmware remains exactly 4 MiB; memory reservations,
CPU count, cache/log geometry and the 0034 kernel are unchanged. Debug symbols
remain the same matching 0034 set.

BOOT size: 23166784 bytes. SHA-256:
`de0622d29fca1635200347e2c14b4f93e75123676d9bfe804589a9da68e13746`.
The installed previous BOOT prefix matches 0034 before writing. Full 0035 image
prefix readback matches after installation; reboot is requested. No SYSTEM or
USERDATA image is rewritten. Activation of the tunable and diagnostic crash
results on the next device run still require a screen or saved-log confirmation.

Raw logs, catalogs, cache pages, process addresses and private firmware artifacts
remain ignored local files. The detailed private analysis is saved under
`out/reports/0034-recovery-incelemesi.md`.
