# 0092: direct executable-leaf lookup on the emulation path

## Measured0091 result

Fresh epoch52 confirms SipHash and bounded PACGA pairing. At2899s the kernel
has24,910,179 traps. The last intact cost snapshot has24,327 successful samples:

| Counter | Cumulative elapsed time |
| --- | ---: |
| Wrapper | 50,885,955,000 ns |
| VM query | 50,656,026,923 ns |
| PACGA callback | 6,542,615 ns |

VM query elapsed time is about99.55% of sampled wrapper elapsed time; hash
elapsed time is about0.0129%. These are sampled wall times, including waits
and preemption, not percentages of total CPU usage. They exclude exception
entry/return. Pairing completed5,212,652 additional instructions with7 fallback
attempts. The four PACGA sites remain frequent last-PC observations.

The complete native export contains90,634 current-boot events. Backboardd
opens B2LHIDTouch and logs type11 events; retained events have no earlier
child-event-size parser error. No current-boot IPS crash report was found in
the collected archive. Neither absence proves overall process/UI stability.
SpringBoard still logs FBSystemShellStartup. Raw logs remain private.

## Source cause and change

The old validator called vm_map_region_recurse_64 with SHORT_INFO. This avoids
resident-page enumeration, but vm_map.c still calls vm_map_region_walk for
an object-backed leaf. That function takes an exclusive VM object lock and
walks/locks shadow objects to gather sharing, reference and pager metadata.
Multiple processes executing the same shared-cache objects can contend on
those locks. None of that metadata is needed to check execute permission.

The validator now acquires a shared range lock on exactly `[PC,PC+4)` with
VMRL_SH_STREAM_NO_HOLES and VMRL_SH_DESCEND_INTO_CONSTANT, advances with the
error-reporting stream iterator, rejects unresolved submaps/non-executable
leaves, and checks the parent-translated/clipped bounds cover all4 bytes.
It releases all range/map locks before fault-recoverable atomic copyin.
Every current/next PC is still validated. No unlocked permission cache, kernel
address relaxation, pointer guessing, pmap shortcut or source-code patch is
introduced. Missing mappings, holes and iterator/lock failures reject the
instruction without changing its saved state.0091's bounded pair, debug gates,
SipHash selection and phase measurements are retained.

## Verification

Actual callback/decoder tests pass ASan/UBSan and ThreadSanitizer for QARMA and
SipHash. The VM boundary harness models range-lock map-pointer invalidation,
constant-submap descent, translated parent bounds and exact4-byte coverage.
It checks failure/missing/non-executable leaves, iterator errors, both bounds,
all locks balanced, copyin only after unlock, and no stale execute-permission
reuse. Pair/register/debug/single-step/fetch-fault tests and concurrent key
publication tests remain passing. This is a boundary test, not host execution
of the complete XNU VM engine.

The real kernel build succeeded. The object audit requires direct range-lock
API relocations and rejects any remaining vm_map_region_recurse_64 relocation
in the ARM64e object, in addition to scalar code/RCpc checks. Installation and
actual change in VM wait time/UI startup need separate device evidence below.

## Installed

The kernel build and scalar/relocation audit passed; no slow region-info call
remains in the ARM64e object and the linked kernel has zero RCpc instructions.
The0091 firmware/loader is reused, retaining SipHash and bounded pairing.

After product/recovery/partition/capacity verification, a full57,671,680-byte
BOOT backup and exact0091-prefix comparison, only BOOT was written. The
23,535,304-byte0092 image and exact readback share SHA256:

`e35b65f9655c4007be83a80ffd8265914d77661ce3978d62a12b38dd6603d6e4`

The reboot command completed. Image, backup, receipt, tests and symbols remain
private in ignored out/0092. The0091 raw region and complete native/crash
archive were saved first. A reduction in vm_ns and successful startup/UI input
still require a fresh0092 runtime log or owner-observed device outcome.

## Owner-observed startup result

After0092 installation the owner reports an Apple logo about1 minute after
power-on and the lock screen about1.5 minutes after power-on. These are manual
owner timings, not instrumented timestamps. Compared with the preceding
80-minute no-UI report, this is strong evidence that the per-trap VM metadata
query was a major startup bottleneck. It establishes lock-screen startup on
this run, not Home-screen access, touch responsiveness or long-run stability.
A fresh0092 cost log is still needed to quantify the actual vm_ns reduction.


## Fresh recovery evidence and Settings black screen

The owner subsequently confirms working touch, then a completely black screen
when tapping Settings. A short power-button press did not restore the image.
Icons also appeared incorrectly rendered. These are owner observations; the
exact Settings launch/failure is not identified in the retained native events.

Read-only recovery collection saved epoch53's16MiB kernel region and a
965,851,169-byte native/supplemental archive. The export receipt reports all
selected files complete, no errors and an unchanged HFS volume header. No
journal replay or device write was performed. Raw evidence stays private in
ignored out/logs/0092-recovery. The kernel decoder reports12 missing positions
and13 producer drops; complete export does not imply complete event coverage.

The current native boot contains53,134 decoded events. No current-boot IPS
report was found, but kernel exit records and native messages establish actual
speech-plugin failures: MauiAUSP25, MacinTalkAUSP10, SiriAUSP9 and
KonaSynthesizer8 exit records. Native exceptions report failure to obtain the
plugin sandbox container, error2. Absence of an IPS does not negate these
crashes. No Preferences process events or retained Settings-launch transaction
were found. There is no retained SpringBoard/backboardd exit or kernel panic
establishing the black-screen cause. UUIDText/empty-string parser warnings and
uncommitted native buffers further limit negative conclusions.

SpringBoard has8 CoreImage render failures for14,400 pixels, each paired with
`destination 12x12 too big`, plus invalid CGContext font-style calls. This is
concrete evidence of incomplete rendering and is consistent with the malformed
icons, but neither the icon identity nor the complete-black-screen cause is
proved. Similar CoreImage failures predate0092 (see0074). The framebuffer
selector79 Unsupported result follows a physical key event; its proximity does
not establish a Settings failure. Chronod's unsupported two-plane YUV requests
are also observed, without a demonstrated connection to Settings. Existing swap
record logging stops after32 records, so absence of later swap messages cannot
be used to conclude that presentation stopped.

The last intact emulation cost snapshot has14,261 successful samples:

| Counter | Cumulative elapsed time | Mean per successful sampled wrapper |
| --- | ---: | ---: |
| Wrapper | 230,339,692 ns | 16.151 us |
| VM query | 70,164,384 ns | 4.920 us |
| PACGA callback | 3,776,038 ns | 0.265 us |

The prior0091 means were about2.092ms wrapper and2.082ms VM query. The0092
sampled mean VM query is about423 times smaller; wrapper about130 times smaller.
These are different runs and sampled elapsed times, not a controlled benchmark,
CPU utilization, or whole-system speedup. At125s the live record has14,602,661
traps, zero flush errors, SipHash active,3,930,079 paired extra instructions
and zero pair fallbacks. Together with the owner startup report, the fresh
measurement confirms that0092 removed a major emulation-path bottleneck.

No new BOOT was installed during this collection. The remaining display issue
needs evidence beyond a generic unsupported-selector result or a presumed
Settings crash.
