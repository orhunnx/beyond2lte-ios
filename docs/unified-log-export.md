# Native unified log export from recovery

Run from this repository with the S10+ in rooted recovery. The exporter targets
only serial `R58M33609TR`, checks `beyond2lte`, and reads `/dev/block/sda31`.
It does not mount, repair, replay a journal, write the phone, or reboot it.

```sh
python3 scripts/pull-b2l-unified-logs.py \
  --output out/logs/0067-recovery/native.logarchive \
  --include-crash-reports
```

The output parent must exist and the `.logarchive` itself must be new. Existing
output is never overwritten. Native `Persist`, `Special`, `Signpost`,
`HighVolume`, `timesync`, `uuidtext`, and `version.plist` paths retain the layout
used by native unified-log readers. Matching uuidtext shared-cache data is
included. Nonessential logd/statistics files are copied if readable.

`--include-crash-reports` also selects **all files**, not only `.ips`, below:

- `/private/var/mobile/Library/Logs`
- `/private/var/root/Library/Logs`
- `/private/var/log`
- `/Library/Logs`
- `/private/var/db/diagnostics`

These extras go below `supplemental/` with their original relative path. Files
already selected for the native archive are not copied twice. Additional exact
absolute directory prefixes can be supplied with repeated `--include-prefix`.
Local raw HFS partition images can be exported with `--image`; that mode makes
no ADB calls.

The exporter reads a fresh HFS volume header, extents-overflow B-tree and catalog
for each run. File data is assembled in logical extent order, including overflow
records. The overflow file itself can bootstrap further extents when its index
path is reachable in its currently mapped prefix; an unreachable bootstrap node
fails explicitly. It validates geometry, linked leaves/backlinks, record offsets,
read lengths, allocation bounds, duplicate extent keys, and internal overlapping
file extents. Metadata forks larger than 512 MiB fail explicitly.

Inspect `b2l-export-receipt.json`:

- `complete`: every essential native log/uuidtext/timesync file exported.
- `all_selected_complete`: supplemental and logd/statistics files also exported.
- Per-file size, SHA-256, extent count, or explicit error.
- Fresh header/catalog/extents hashes and the final header stability check.

A complete receipt describes the **observed disk snapshot**. Read-only export
cannot recover buffered logs or replay an uncommitted HFS journal. A stable
header in recovery does not make this a hardware atomic snapshot. Files may
retain native redactions, and decoding needs OS-compatible tooling. Supplemental
logs may contain private information; keep the archive local during analysis.

Local verification:

```sh
python3 tests/test_pull_b2l_unified_logs.py
```

The synthetic HFS fixture covers a ninth file extent in an overflow tree with an
index node, linked catalog leaves, native and supplemental path selection,
short reads, corrupt B-tree offsets/links, missing extents, unsafe output paths,
and a changing header that must mark the receipt incomplete.

## Observed 0067 acquisition

The 0067 recovery export read 549 files (773,995,148 bytes), including 98 tracev3
files and 78 crash reports. Both completeness fields were true and the HFS
header stayed unchanged. The volume was dirty; no journal was replayed.

Apple log show/collect rejected this manually copied iOS datastore. For this
acquisition, Mandiant macos-UnifiedLogs v0.7.0 decoded the native files locally.
Its log-archive reader expects UUID text directories at the archive root; local
links from the exported uuidtext children can provide that view. Keep the raw
export receipt and distinguish copied bytes from third-party parser results.
The parser emitted missing-UUIDText warnings for some libraries, and its wall
clock output has not been calibrated for the custom platform.

The full derived output was streamed through gzip to avoid a very large plain
JSONL file. GUI-specific evidence and boot-separated counts are under
out/logs/0067-recovery/. No additional debug logging was enabled by this read.
