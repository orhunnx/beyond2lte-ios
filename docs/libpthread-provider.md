# Built-in Apple OSS libpthread dependency

The standalone B2L kernel has no external kernel-extension loader. It compiles
Apple's real kernel pthread provider into the image, following the A5 port's
public `0019`/`0102` integration pattern while checking the current ABI.

| Input | Immutable revision |
| --- | --- |
| XNU | `ac9718fb1af618d5ce8678d0dc6e8a58f252216f` |
| Apple OSS libpthread | `libpthread-539.100.4` |
| Peeled source commit | `1f4f5265b319111142f1bf3a27d4484ef5a98314` |
| Annotated tag object | `68ea242a599b7fc735cfb3e38467c9ed96097d0f` |

Source: [official Apple OSS commit](https://github.com/apple-oss-distributions/libpthread/tree/1f4f5265b319111142f1bf3a27d4484ef5a98314).
The version is paired with the macOS 26.5 release, rather than selected from a
moving main branch or inferred from the older A5 provider.

## Reproducible staging

`scripts/build-kernel.sh` automatically runs `scripts/stage-libpthread.py` before
invoking XNU's standalone build target. On first use, the staging script fetches
the immutable source into ignored `out/dependencies/libpthread-539.100.4`.
It verifies both the XNU and provider commits, then copies only the upstream
`kern`, `private` and `include` trees into the local XNU checkout's
`bsd/pthread/b2l_libpthread` directory. Original copyright/license notices stay
with those files. The fetch is approximately 2 MiB; no prebuilt pthread kext is
loaded or committed.

An existing matching source checkout can be supplied without another fetch:

```sh
LIBPTHREAD_SOURCE=/path/to/libpthread-539.100.4 \
  KDKROOT=/path/to/merged-kdk B2L_DEPSROOT=/path/to/deps \
  scripts/build-kernel.sh CWARNFLAGS=-w CXXWARNFLAGS=-w
```

The stage script applies checked source compatibility edits: explicit
pointer-to-address casts at callback boundaries, integer zero for user-address
sentinels, and portable panic format strings. The wrapper retains the provider's
kext header view, aliases the exported seven-argument `vm_fault` ABI and isolates
provider-owned names from XNU's shim names. Actual synchronization and thread
algorithms remain upstream implementations.

The public SDK `TargetConditionals.h` is copied into this isolated local input
so XNU's `-nostdinc` provider compiles can resolve it. It is not committed.
No KDK private header, proprietary binary, provider source checkout or staged
upstream tree is copied into this repository's overlay.

A manifest records the immutable source commit and hashes every staged file.
Subsequent builds verify that manifest and refuse a missing, stale or modified
provider instead of overwriting local edits or silently accepting a different
revision. Inspect and remove an intentionally obsolete local staging directory
before restaging it; the script never resets a user's checkout.

## ABI and tests

Only B2L increases the kernel's opaque wait-element reserve from 48 to 56 bytes,
with alignment 8. Provider compile-time assertions check size, alignment and
`kwe_uth` offset 48. XNU compiles all three real provider translation units with
its current callback signatures, providing an actual ABI compilation gate.
The upstream version-0 function table is preserved.

```sh
python3 tests/test_libpthread_provider.py /path/to/patched/xnu
```

The host test checks actual provider registration and initialization sequence,
layout/canaries, all 61 callback dependencies, missing/duplicate registration
panics, immutable staging and refusal of edited staged files under ASan/UBSan.
Hosted kernel hooks do not emulate scheduling or prove userspace pthread
operation. Those remain physical runtime tests.
