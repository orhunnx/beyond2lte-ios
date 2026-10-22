# Build 0034: preserve corecrypto measured text

## Scope

The user authorizes excluding only the corecrypto code measured by the failing
FIPS integrity test. The kernel dynamically resolves that exact `__TEXT,__text`
section, restores previously translated bytes after root journal replay and
before userspace, and refuses future persistent translations in that range.
Neighboring code in the same subcache retains its native translations. Runtime
emulation handles the excluded instructions. The FIPS tests are not altered.

EFI, firmware layout, CPU count, memory arena, SYSTEM cache/log geometry and the
existing emulator are unchanged. SYSTEM and USERDATA are not reimaged. Cached
original pages remain durable undo data; restoration uses the normal mounted
root VFS and synchronous flush/readback.

## Verification and installation

- ASan/UBSan native-cache format/conversion and all torn-record/root-write
  prefixes pass. Real SYSTEM HFS placement and metadata/journal rejection pass.
- New exclusion tests pass module/UUID resolution, invalid mapping/command
  rejection, boundary preservation and interrupted selective restoration.
- The actual private cache resolves 554192 measured bytes and 589 translated
  words across 79 pages. Every restored byte matches pristine code. Adjacent
  translated bytes in the same file remain native.
- Actual kernel object flags/disassembly verify scalar code generation for all
  cache objects and the ARM64e/VM resolver. Matching kernel, unstripped image and
  dSYM have UUID `F2EF95EB-85F9-3767-9EBE-EA22D1B63F46`.
- The 23166784-byte BOOT has SHA-256
  `4f47ca465ead215d27c3644e936f44f3bb251ce57279c03ecfe1791d2d05a1e0`.
  Before writing, the physical BOOT prefix matches retained 0033. The complete
  0034 prefix is read back and its hash matches. Reboot is requested.

The first user screen reaches `init_featureflags`, with `applied=246`,
`learned=854`, `skipped=1913`, `dropped=128724`, `errors=0`, and persistent-log
`flush_errors=0` / `last_error=0`. This verifies continued cache application and
learning on device. The frame contains no panic, but does not show the final
FIPS aggregate result or the pre-init restoration line. FIPS integrity success
and completion of boot remain unverified; no startup speed claim is made.

Private image/cache contents, logs, photographs and filesystem catalogs are
kept in ignored local artifacts and are not part of the source commit.
