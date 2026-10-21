# Build 0023: unsigned development shared cache

## Device evidence and change

Build 0022 reaches dyld/libignition, then fails shared-cache signature registration. The missing standalone `libSystem.B.dylib` is the subsequent dyld failure; the library is supplied by the existing shared cache.

Patch 0012 adds a B2L-only `F_ADDFILESIGS_RETURN` path when global system signature enforcement is disabled. It checks the regular vnode size, nonnegative file base, signature offset and length, and the existing 40 MiB signature size limit using subtraction before returning the unsigned file extent. It propagates VFS and copyout errors and releases the vnode reference. It never attaches a cs_blob, marks VM pages signed, or fabricates entitlements. Other commands and enforcement-enabled operation retain the upstream path.

Both shared-region and configuration maps follow the same global enforcement policy on B2L. Other boards remain enforced. Existing CodeDirectory page-size admission and hash verification code are unchanged, avoiding unsafe 16 KiB hash reads from 4 KiB physical pages.

## Verification

The actual extent checker and fcntl branch are compiled with ASan/UBSan and mocked VFS/copyout functions. Cases cover valid cache extents, negative bases, offset/size overflow, truncated files, the enforcement and command gates, VFS failures, copyout failure, and vnode cleanup. The pinned twelve-patch sequence passes fresh application, upgrade, idempotence, and local-edit refusal. Native kernel compilation, package bounds, embedded-loader validation, and matching stripped/unstripped/dSYM UUIDs also pass.

Installation targets BOOT only. USERDATA, SYSTEM, GPT, recovery, physical RAM ranges and reserved regions are not changed. Binaries, cache files, symbols, identifiers and installation receipts remain ignored local artifacts. Runtime shared-cache mapping, subsequent ARM64e execution and launchd services require device evidence after installation; this build does not claim those successes.
