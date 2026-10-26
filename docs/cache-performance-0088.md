# 0088 cache and trap performance investigation

## Preserved evidence

The private0088 epoch49 kernel tail preserves412 periodic B2L LIVE snapshots
from approximately22,000-34,350 seconds. The last observation reports514,462,971
traps and514,462,926 successful emulations. The last cache summary reports
queued141,248; learned30,961; applied159; skipped71,227; dropped7,850,784;
pending0; active0; four workers; errors0. Learned counts persisted instruction
updates; applied counts changed source pages before userspace. These are not
the same unit or a whole-system conversion percentage.

## Dominant sampled instruction family

The runtime shared-cache slide0x166e4000 is independently anchored by the
current-boot crash's mach_msg2_trap address0x2495f1cd4 and its original
IPSW address0x232f0dcd4. Reading original code at the sampled addresses gives:

| Runtime PC | Unslid PC | Samples | Instruction | Native function |
| --- | --- | ---: | --- | --- |
|0x199bde8e4|0x1834fa8e4|175|PACGA x8,x0,x8|__CFCheckCFInfoPACSignature +28|
|0x199bde8e0|0x1834fa8e0|63|PACGA x8,x8,x10|__CFCheckCFInfoPACSignature +24|
|0x199bd7bf0|0x1834f3bf0|67|PACGA x8,x21,x8|_CFRuntimeCreateInstance +684|
|0x199bd7bec|0x1834f3bec|57|PACGA x8,x8,x10|_CFRuntimeCreateInstance +680|

These four sites account for362/412 snapshots (87.86%). This is a periodic
last-PC observation distribution, not a CPU-time profile or an exact instruction
execution distribution. PID141, the most common sampled process (275 snapshots),
is homed according to the current-boot native log. The checker masks CFInfo,
uses modifier0x82c in the first PACGA, mixes the object address in the second,
then checks the result and can BRK on mismatch. A constant replacement or NOP
would not preserve its result.

The actual b2l_nc_native converter was executed on these opcode words and
returned0 for every PACGA site. RETAB control examples return1. Thus persistent
conversion cannot remove the observed PACGA hot path. This explains why more
learned PAC/AUT/return instructions do not necessarily eliminate the main sampled
trap workload. LDRA is also excluded, but is uncommon in these top samples.

## Additional costs established by source

Every successful PACGA currently takes the same b2l_compat_key_lock mutex to
initialize/read a16-byte key, even after that boot-global key becomes immutable.
It then computes QARMA5 in software. Emulation also validates the current and
next executable PC through VM region lookup/copyin and copies the saved register
context. All eight CPUs share the key mutex and global telemetry atomics. These
are real operations; their separate hardware-time costs have not been measured.

The learner has a separate127-entry usable shared queue guarded by a nonwaiting
try-lock. Four workers prepare data concurrently, but durable commits serialize
through one mutex. Each new learned instruction persists and reads back a12KiB
A/B record, plus header publication when extending the high-water mark. It does
not gather every instruction on a page in a single commit. The hint table marks
a queued observation immediately, before resolution/persistence succeeds. A
failed/transient worker observation can therefore suppress retries until that
direct-mapped hint is replaced. This is a code-path limitation, not a quantified
explanation for the71,227 skips.

The dropped counter combines try-lock contention, queue-full rejection and lack
of a free disk slot. The skipped counter combines VM/vnode resolution, file
identity, source-word mismatch, measured-code exclusion and stale records. The
old counters cannot attribute those categories. pending0 at the final sample
does not exclude earlier queue bursts.

## Next concrete changes

1. Preserve exact PACGA/QARMA results. Publish the immutable boot key with
   release/acquire ordering so the steady path does not take its mutex.
2. Evaluate a bounded per-CPU result cache keyed by both PACGA operands and the
   boot key lifecycle. Measure hits/misses, computation time and migration/race
   behavior; a result must never be reused for different operands or keys.
3. Split admission/skip counters and record queue high-water and commit duration
   before attributing disk-learning drops. Revisit hint retry semantics for
   transient failures and group same-page updates while retaining durable A/B
   publication, flush and readback.
4. Compare a fresh boot's apply/reclaim/undo totals and traps per elapsed second.
   0089 is installed, but its log was unavailable during this investigation;
   whether it applied0088's newly learned pages is not yet verified.

No new device image or production source change was made for this investigation.
The personalized logs, local opcode extracts and symbol evidence remain ignored
under out/logs/0088-recovery and out/research-cache-0089.
