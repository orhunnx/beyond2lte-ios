# B2L framed persistent console

This original implementation records `b2l_early_putc` output in a fixed ring and
flushes it from a dedicated kernel worker. It does not change root selection,
mount a filesystem, format storage, alter RAM/MMIO guards, or expose a writable
BSD device for its log region.

## Integration contract

Compile `osfmk/arm64/b2l_log_core.c` and `b2l_persistent_log.c` only for B2L, with
`-mgeneral-regs-only`. Export both headers if consumers use the generated SDK.
Add exactly one `b2l_log_append(c)` call at the beginning of `b2l_early_putc`,
before its framebuffer-active test. Other console paths must not append again.

After native UFS disk initialization and outside the disk mutex, the parent
adapter may call `b2l_log_start(&sink)` when its log boot argument is enabled.
The sink callbacks are synchronous and use offsets relative to the dedicated
16 MiB tail region. Each callback returns zero for complete success and nonzero
on failure. The callback owner must independently verify SYSTEM/HFS/tail bounds,
serialize its private reader, and propagate device write/synchronization errors.
The kernel log layer does not infer or broaden write authorization.

Before writing anything, `b2l_log_start` reads and validates the two typed region
headers. Both must have exactly the supported magic, version, page geometry and
CRC; a single intact header is sufficient if the other was torn. If neither is
valid, the kernel refuses to write. It never initializes an unformatted region.
The valid header with the highest epoch wins. A new epoch is written to the
opposite header and synchronized before worker startup or any data record.
Epoch exhaustion fails closed.

Provision a new private raw region file with:

```
python3 scripts/b2l-log-region.py init NEW_REGION_FILE
```

This creates exactly 16 MiB and refuses an existing output. Provisioning that
file onto the authorized tail belongs to the parent storage integration.

## RAM and concurrency

The ring retains up to 4 MiB of text. Each character is published as one aligned
64-bit atomic word containing its byte and a 56-bit sequence tag. Ring storage is
therefore 32 MiB plus 16 bytes of counters; the complete native worker object has
33,558,640 bytes of zero-fill storage including its writer buffer/state.

Append allocates no memory, takes no locks, calls no console/disk callbacks, and
requires no scheduler, TLS or initialized kernel lock. Sequence allocation,
publication and best-effort loss counting each use at most four weak CAS
attempts. Weak CAS avoids an implicit unbounded reservation retry on pre-LSE
ARM. A delayed writer cannot replace a newer generation after wrap. Reads only
accept the exact requested sequence tag. Sequence exhaustion stops allocation
rather than wrapping its tag.

The design intentionally loses data under contention, ring wrap or interrupted
publication. The producer-drop counter is a lower bound if its own bounded
update contends. Overwritten byte positions appear as sequence truncation/gaps;
allocated but uncommitted positions have a clear validity bit. A writer that
resumes after a snapshot was flushed cannot retroactively change that snapshot.
No complete panic transcript is promised.

## Disk framing and timing

The worker drains at most 128 pages per batch, then performs one real sink sync.
It waits five seconds between batches. Disk callbacks never emit recursive log writes. The worker emits one
telemetry line about every thirty seconds outside all storage locks/callbacks. Failed write/sync leaves the in-memory persisted cursor/index unchanged,
so a later retry rewrites the same record positions. A failed or unsynchronized
write is not reported as durable by this state machine.

All integer fields are little-endian. CRC32 uses polynomial `0xedb88320`, initial
all-ones/final inversion, over a complete 4096-byte page with bytes 48..51 zero.

The two 4096-byte headers are at offsets 0 and 4096:

| Offset | Field |
| --- | --- |
| 0 | 8-byte magic `B2LLOG01` |
| 8, 12 | u32 version 1, header size 4096 |
| 16 | u64 region size 16777216 |
| 24, 28 | u32 record size 4096, record count 4094 |
| 32, 40 | u64 data offset 8192, boot epoch |
| 48 | u32 CRC32 |
| 52..4095 | Must be zero |

The data area contains 4094 cyclic pages, indexed by record index modulo 4094:

| Offset | Field |
| --- | --- |
| 0 | 8-byte magic `B2LLOGPG` |
| 8, 12 | u32 version 1, header size 64 |
| 16, 24, 32 | u64 boot epoch, record index, first text sequence (zero based) |
| 40, 44, 48, 52 | u32 span (1..3584), valid count, CRC32, flags=0 |
| 56 | u64 best-effort cumulative producer-drop counter |
| 64..511 | 448-byte validity bitmap, low bit first |
| 512..4095 | 3584 text-byte positions |

A validity bit is set only if the ring held exactly that text sequence when
copied. Unused bits and payload bytes are zero. CRC, epoch and ring-position
validation let recovery discard torn or stale pages. CRC is corruption detection,
not authentication. A record may be recoverable even if its final sync did not
return; its existence is not a promise that all later panic bytes survived.

Decode a saved region or full image using:

```
python3 scripts/b2l-log-region.py decode IMAGE --offset REGION_OFFSET --text LOG.txt
```

The decoder selects the latest valid epoch, reports ignored pages, truncation,
missing positions and the producer-drop lower bound. It inserts explicit gap
markers instead of silently joining text across missing positions.

## Verification and limits

`bash tests/test-persistent-log.sh` runs actual-source ASan/UBSan tests with a reduced ring size:
8 concurrent writers and a reader, 400,000 sequence-tagged publications, 240,000
bounded append attempts, stale-writer wrap protection, sequence exhaustion,
header corruption, absent-header no-write, geometry rejection, epoch rotation,
partial-write and sync-error rollback, missing-position bitmap, disk-slot wrap,
and Python decoding of records emitted by the actual C writer. Both complete
production translation units also compile with the current native XNU arguments.
Native object inspection confirms no external atomic helper or lock dependency
on the append path; its instructions use general registers only.

These are host/build checks. The hardware proof remains a valid recovered record
from the next boot's log region. A power loss, hard lockup, early failure before
native UFS initialization, or failure in the disk path can prevent persistence.

## Recovery extraction

Run `python3 scripts/pull-b2l-log.py --serial DEVICE_SERIAL --output out/logs/BOOT.txt`
from rooted beyond2lte recovery. The tool checks recovery/device identity, reads
only the final 16 MiB of SYSTEM, saves the private raw region beside the decoded
text and refuses existing output paths. It never writes a phone partition.
The decoded hardware log, screenshots and raw region remain ignored artifacts.
