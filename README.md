# beyond2lte-ios

Boot Apple's open source XNU kernel on the Samsung Galaxy S10+ SM-G975F
(`beyond2lte`, abbreviated **B2L**, Exynos 9820).

The first milestone is kernel entry, framebuffer console output and real timer
interrupt delivery on the device. iOS userspace and SpringBoard are later
milestones. An IPSW is not required for this initial kernel boot image.

## Boot chain

`Samsung S-Boot → BootShim → beyond2lte UEFI → embedded XnuLoader → XNU`

The existing [beyond2lte UEFI port](https://github.com/orhunnx/beyond2lte-uefi)
is an external firmware dependency. This repository contains the XNU board
changes, standalone EFI applications and packaging scripts. ACPI and Windows
implementation files are not copied here. The XNU firmware profile omits ACPI
and selects the XNU loader as its default boot application. U-Boot is not used.

## Source baseline

- [XNU `rel/xnu-12377`](https://github.com/apple-oss-distributions/xnu/tree/rel/xnu-12377),
  pinned to `ac9718fb1af618d5ce8678d0dc6e8a58f252216f`.
- Generic ARMv8-A with crypto instructions and 4 KiB pages; the current
  target uses one Cortex-A55 CPU. The isolated two-core 0032 experiment reset
  early; 0033 restores the proven single-CPU topology.
- GICv2 at `0x10101000` / `0x10102000`; virtual timer INTID 27.
- Actual architected counter frequency read at runtime; expected 26 MHz.
- Apple PAC, BTI, SPTM and PPL are not used by this initial B2L target.

See [0033](docs/build-0033.md) and the [automatic native cache](docs/native-cache.md)
for read-only EFI staging and journal-safe pre-init code-page reuse.

## Build and package status

The standalone ARM64 kernel, debugging symbols, EFI loader and external 4 MiB
firmware profile build locally. Packaging produces a single Android v1
`boot.img` with the firmware and a bounded XNU payload trailer. The embedded
loader reads that payload through the existing firmware UFS service from GPT
partition `BOOT`; no separate ESP or SYSTEM/USERDATA transfer is required.

**The first kernel-boot milestone is achieved on the physical device: XNU owns
its framebuffer console, completes VM startup and reaches BSD root discovery,
registers its boot
CPU, and receives real timer/self-SGI interrupts with timed scheduler wakeups.
Build 0011 reaches a root-device wait for Apple_HFS without a panic. Native
read-only UFS access is also verified on the device in build 0012. Build 0015
mounts the populated native USERDATA HFS root read/write, then
rejects the ARM64e launchd executable with EBADARCH. Later compatibility builds
reach real iOS userspace; the recovered 0031 log confirms service startup but
also repeated SpringBoard/user-manager crashes. A usable graphical session
remains unverified.**
[Build 0004](docs/build-0004.md) supplies the required 256-byte seed; its
device result identified the next DT contract gap. [Build 0005](docs/build-0005.md)
adds the required /chosen/memory-map and /defaults containers.
[Build 0006](docs/build-0006.md) adds allocator RNG and real kernel PRNG startup;
its device test passes PRNG registration and exposes the console lifecycle gap.
[Build 0007](docs/build-0007.md) restores initialized console locks and the real
display-control backend while preserving early text. Its device test passes
console attachment and reaches the missing Image4 provider.
[Build 0008](docs/build-0008.md) adapts the A5/SWEET 0015 unavailable-provider
handling to the current trust-cache APIs. Its BOOT-only installation is verified;
the device photograph confirms progress into BSD startup and real IRQ proof.
[Build 0009](docs/build-0009.md) links and registers the matching open-source
libpthread kernel provider. Its device test passes into network initialization;
the next failure identifies the absent AES mode provider.
[Build 0010](docs/build-0010.md) adds real portable AES CBC/ECB and rejects
unavailable GCM operations safely. Its device test passes into BSD autoconfiguration
and exposes a missing boot-policy property.
[Build 0011](docs/build-0011.md) supplies the explicit existing non-CTRR policy
in the Apple device tree. Its device photograph confirms the root-device wait
without the previous panic.
[Build 0012](docs/build-0012.md) adds native read-only UFS transport and bounded
BSD disk/GPT access, adapted from the working UEFI driver. Its BOOT-only
installation and physical native UFS capacity/GPT/signature reads are verified.
The subsequent authorized [empty SYSTEM HFS+ image](docs/system-hfs.md) is a
separate storage preparation step.
[Build 0013](docs/build-0013.md) links the real OSS HFS provider, selects the
validated SYSTEM root and adds bounded SYSTEM writes with real cache flushing.
The device photograph verifies the read/write HFS root mount and a completed
native write/cache flush. Startup then exposes a separate absent-AMFI entitlement
helper fault; the empty volume contains no init or iOS userspace.
[Build 0014](docs/build-0014.md) handles unavailable AMFI entitlement callbacks
without granting privileges. Its device test initially stopped on the dirty
non-journaled volume left by the prior panic. SYSTEM was then
[repaired and converted to Journaled HFS+](docs/system-journal.md), with only the
changed blocks written back. The subsequent 0014 device photograph confirms
another successful read/write root mount, progress past the AMFI fault and an
attempt to load `/sbin/launchd`. The final ENOENT panic is expected because that
file and iOS userspace are not present.
[Build 0015](docs/build-0015-arm64e.md) selects the populated USERDATA root.
Its device photograph confirms the read/write mount and a launchd load attempt,
ending with errno 86 (EBADARCH). Recovery instruction probes catch PACIA as
SIGILL on every core; a coherent ARM64e execution and dyld/cache implementation
is required before this iOS userspace can run.
[Build 0016](docs/build-0016.md) adds a resident kernel ARM64e compatibility
PoC, with scalar assembly control-flow handling, software QARMA5 PACGA and
16 KiB cache-slide / 4 KiB VM support. Full kernel/EFI/BOOT builds and focused
host tests pass. BOOT installation was readback-verified and reboot accepted;
physical EL0 execution remains unverified. Signed execution and other iOS
services remain follow-on work.
Subsequent device tests through [0027](docs/build-0027.md) demonstrate real
ARM64e launchd and shared-cache execution, followed by a startup assertion.
[Build 0028](docs/build-0028.md) adds A5-style development Sandbox, work-interval
and AMFI XML/platform metadata support. Its first device test exposed a late
policy-registration fault; [0028b](docs/build-0028b.md) moves registration before
lockdown. [0029](docs/build-0029.md) adds the boot-task identifier query after
0028b reaches real launchd boot tasks. See the [shim audit](docs/shim-audit.md)
for confirmed gaps and the distinction between local tests and runtime proof.
The 0029 device subsequently reaches keybagd; [0030](docs/build-0030.md) adds a
scoped [volatile development KeyStore](docs/development-keystore.md) for this
no-passcode HFS bring-up.
[0031](docs/build-0031.md) adds framed SYSTEM console persistence, a reserved-range
aware larger RAM profile and the launchd development cache boot option.
BOOT writes retain the original Android backup and are readback-verified.
[Build 0002](docs/build-0002.md) uses a dynamic 1–2 GiB RAM arena within the
unchanged firmware memory guards. [Build 0003](docs/build-0003.md) adds XNU-owned
text from assembly entry through C and the normal printf/kprintf console.
It does not contain an iOS root filesystem. Progress after the scheduler may
stop while looking for a root device; that is separate from initial kernel
entry and interrupt bring-up.

Local artifacts are placed under ignored `out/` directories. Firmware binaries,
SDKs, KDKs, IPSWs and local upstream source checkouts are not committed.

## Build workflow

1. Fetch the pinned source with `scripts/fetch-xnu.sh`.
2. Apply the board changes with `scripts/prepare-xnu.sh`.
3. Prepare external dependencies described in [build notes](docs/build.md),
   then run `scripts/build-kernel.sh`.
4. Build the standalone EFI applications and external firmware profile using
   `scripts/build-boot-firmware.sh` in a disposable Mu work tree.
5. Create the image with `scripts/package-boot.py` as described in
   [packaging](docs/packaging.md).

## Verification and device boundary

```sh
scripts/test-loader.sh
scripts/inspect-kernel.sh /path/to/mach.development.b2l
python3 scripts/package-boot.py --verify /path/to/boot.img
```

Host checks cover malformed Mach-O input, Apple device-tree bounds, the exact
XNU boot ABI and BOOT container arithmetic. Packaging also verifies that the
actual compressed firmware volume contains the selected EFI loader.

No phone partition is written by these build/package commands. The user's
explicit approval is required before an ADB BOOT write. The connected device
must first be verified as beyond2lte and its real BOOT capacity checked.

See [the boot contract](docs/boot-contract.md),
[EFI handoff](docs/efi-xnu-loader.md) and
[runtime milestones](docs/runtime-bringup.md) for execution details and the
screen output needed to establish a successful device test. Native disk/root
prerequisites are recorded in [storage next steps](docs/storage-next.md).

[0032](docs/build-0032.md) integrates a bounded two-core startup experiment,
with per-core timer and bidirectional remote-IPI proof before publishing 2/2 CPUs.
The known single-core 0031 package is retained as rollback.

[Build 0036](docs/build-0036.md) adds bounded crash reasons and user backtraces
after the recovered 0035 log confirmed repeated service aborts. BOOT readback
and reboot are verified; a working graphical session remains unverified.

[Build 0037](docs/build-0037.md) targets the recovered SpringBoard
platform-status assertion and adds the missing development IOWatchdog service.
BOOT readback and reboot are verified; runtime confirmation is pending.

[Build 0038](docs/build-0038.md) adds missing-only persona metadata initialization
after the recovered daemon log identifies ENOENT for its manifest. BOOT readback
and reboot are verified; daemon startup and UI remain runtime tests.

[Build 0039](docs/build-0039.md) supplies original DER entitlement metadata
through the iOS 26 SecTask query interface, targeting shared-container and lsd
failures. BOOT readback and reboot are verified; the recovered log still shows
lsd aborts, so that failure remains unresolved.

[Build0040](docs/build-0040.md) reintroduces SMP for the four A55 and two A75
cores, using a fresh MAX_CPUS6 build and per-core early startup diagnostics.
M4 cores remain absent. The single cache worker is retained until real per-core
timer/IPI proof and sustained runtime have been confirmed on the device.

[Build0041](docs/build-0041.md) separates provider arrival from ordered CPU0-first
registration after0040's rapid reset. Fine-grained early registration/scheduler
markers are added; six-core hardware proof remains pending.

[Build0042](docs/build-0042.md) uses an EFI-allocated low physical secondary
trampoline after0041 reaches CPU_ON with a high entry before resetting. The
kernel stays in its existing high arena; reservation descriptors are unchanged.
Hardware CPU_ON acceptance and six-core proof remain pending.
