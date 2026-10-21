# Build notes

## Local toolchain

The initial local build uses an Apple Silicon Mac, Xcode 27.1, the macOS 27
SDK for host tools, and an iPhoneOS target of 26.5 for the kernel. The private
kernel headers come from Apple's signed Kernel Debug Kit 26.5 build 25F71.
Both KDK package payloads (`KDK.pkg` and `KDK_SDK.pkg`) are required.
SDKs and KDKs are external dependencies and must not be committed.

Additional pinned Apple open source dependencies:

| Component | Commit | Purpose |
| --- | --- | --- |
| AvailabilityVersions | `149b1777f3e8c2133042d8e188f72e3adf6a7110` | Availability definitions and generator |
| libdispatch | `701f4d1a24ae9c6863901bbbb22624b7d1b87321` | ARM64 iPhoneOS `libfirehose_kernel.a` and private headers |

The firehose library must target iPhoneOS, not macOS. Do not link the KDK's
ARM64e SoC or Tightbeam libraries into the generic ARM64 B2L image.

## XNU source changes

Fetch the pinned XNU source and run `scripts/prepare-xnu.sh`. It applies
`patches/0001-b2l-machine-and-local-dependencies.patch` and the new source
files under `xnu-overlay/` without resetting a checkout. Use a separate
object directory for B2L and iPhoneOS. Export lists from a previous macOS
configuration are not interchangeable with iPhoneOS export lists.

`B2L_DEPSROOT` names an isolated dependency installation root. It contains
`usr/local/libexec/availability.pl`, exported XNU/private firehose headers and
`usr/local/lib/kernel/libfirehose_kernel.a`. The patch adds this root to the
kernel's include and library search paths.

The initial target is `DEVELOPMENT ARM64 B2L`, with `PLATFORM=iPhoneOS`,
`PLATFORM_FOR_CONFIG=iPhoneOS`, `SDKVERSION=26.5`, `BTI_BUILD=0` and
`BUILD_LTO=0`. The linker disables chained fixups for the standalone loader.
Use `scripts/build-kernel.sh` after preparing these external dependencies:

```sh
KDKROOT=/path/to/merged-kdk B2L_DEPSROOT=/path/to/deps \
  scripts/build-kernel.sh CWARNFLAGS=-w CXXWARNFLAGS=-w
```

The standalone `build` target produces the kernel and debugging symbols.
The upstream `all` target additionally creates Apple kext KPI bundles; its
export lists reference Apple IOMMU/monitor symbols absent from B2L. Those
bundles are not currently supported. The local first-boot build disables
upstream compiler warnings with the shown overrides; it is not a claim of a
warning-clean build against the newer SDK. Current device evidence and remaining milestones are recorded in
[build 0002](build-0002.md).

## EFI applications

`uefi/B2LXnuPkg.dsc` is a standalone EDK II package. It does not include the
Samsung firmware platform, ACPI tables or Windows components.

The local build uses Mu Basecore commit
`762a65dad79e2934cddbefa467b7e2c7b5ee4bc0`, native BaseTools, Python 3 and
LLVM's `clang`, `llvm-lib`, `llvm-rc` and `lld-link`. The CLANGPDB toolchain
requires `lld-link`; a Clang installation alone may not provide it.

Copy `uefi/` into the EDK II workspace as `B2LXnuPkg/`, set `WORKSPACE`,
`EDK_TOOLS_PATH`, `EDK_TOOLS_BIN`, `PACKAGES_PATH` and `CLANG_BIN`, then run:

```sh
build -a AARCH64 -t CLANGPDB -b RELEASE -p B2LXnuPkg/B2LXnuPkg.dsc
```

Both `BootSurvey.efi` and `XnuLoader.efi` build. The former only reports the
boot boundary; the latter reads the packaged kernel and enters XNU after
exiting boot services. See [EFI loader details](efi-xnu-loader.md).

For a BOOT image, use a disposable external firmware work tree containing the
reference beyond2lte platform overlay, then run:

```sh
WORKSPACE=/path/to/disposable-mu \
PYTHON_COMMAND=/path/to/venv/bin/python \
CLANG_BIN=/path/to/llvm-tools/ scripts/build-boot-firmware.sh
```

This first builds the app, prepares the XNU firmware profile and rebuilds the
external FD with its original 4 MiB size. [Packaging instructions](packaging.md)
cover the final Android image and its XNU trailer.

## Host parser checks

```sh
scripts/test-loader.sh
```

The tests use AddressSanitizer and UndefinedBehaviorSanitizer. They validate
the parser's malformed-image handling; they do not simulate XNU execution.
