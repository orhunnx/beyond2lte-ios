# Build 0033: automatic native-instruction cache

This build returns to the proven single-CPU configuration after 0032's early
reset. Patch 0023 reverses the upstream SMP-specific changes from 0022; the
single-CPU IOCPU provider and Apple device-tree topology are restored. The
4024 MiB conventional-memory arena and 16-page bootstrap table budget remain.

See [the automatic cache design](native-cache.md) for the learned-instruction,
read-only EFI staging, HFS replay/VFS application and power-loss behavior.
The kernel retains the existing runtime emulator for unsupported conversions.
The populated USERDATA image is not reformatted or transferred again.

## Checks

- Patch sequence upgrade 0022 to 0023, idempotence and local-edit refusal pass.
- ASan/UBSan core tests check SHA-256, native encodings, unsupported instructions,
  stale content, dual records, all torn-record prefixes and partial root pages.
- Real provisioned HFS image passes journal-empty, contiguous-file and exclusion
  of HFS metadata/journal/volume headers checks. Existing log-tail bytes remain.
- Loader DT/arena/boot-argument ABI and ARM64e/QARMA5/mixed-pager regressions pass.
- EFI builds successfully within the unchanged 4 MiB firmware allocation.

Installation and runtime results are recorded below once obtained. Successful
build/readback does not establish cache learning, reuse or performance on device.

## Installation

The final scalar-only kernel, unstripped image and retained dSYM have matching
build UUIDs. Actual compiler arguments and disassembly of all four cache objects
and the ARM64e/VM-resolver object contain no SIMD/FP instructions.

The 23,166,168-byte final BOOT is installed and its complete prefix readback
SHA-256 matches `b270f179ed03fc5e0b8d891a9a4ca3d5056bd871334c184dd913ea862cf86b72`.
The complete SYSTEM image readback also matches its prepared SHA-256. Its
existing 16 MiB log tail is preserved byte-for-byte. USERDATA is not reformatted
or retransferred, and its header is unchanged before reboot. Reboot is requested.

Hardware cache staging/learning and a later apply/reuse cycle still require the
device screen or persistent log; no startup-speed improvement is claimed yet.

## First device runtime result

The user's first 0033 screen confirms the cache learner running on device.
`learned` increases from 3223 to 3476, with `errors=0`, while the persistent
console reports `flush_errors=0` and `last_error=0`. `applied=0` is expected for
this initially empty cache. These are successful durable record updates, not a
count of distinct 4 KiB pages or a measurement of acceleration.

The bounded observation queue drops 423100 optimization requests in the later
sample. This does not reject their execution: the existing emulator remains
active. USERDATA remains mounted as journaled HFS, and mount helpers exit zero.
The next boot must show nonzero pre-init `applied`/`already` counts before the
complete automatic reuse cycle can be called hardware-verified.
