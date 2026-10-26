# beyond2lte-ios

Boot Apple's open source XNU kernel on the Samsung Galaxy S10+ SM-G975F
(`beyond2lte`, abbreviated **B2L**, Exynos 9820).

The port boots XNU and iOS userspace on the physical device. SpringBoard UI
has appeared, and all eight CPUs have passed timer and cross-CPU interrupt
checks. This is an experimental bring-up: UI responsiveness, service stability
and hardware coverage remain incomplete. An IPSW is needed for iOS userspace,
but not for a standalone kernel/console boot.

## Boot chain

`Samsung S-Boot → BootShim → beyond2lte UEFI → embedded XnuLoader → XNU`

The existing [beyond2lte UEFI port](https://github.com/orhunnx/beyond2lte-uefi)
is an external firmware dependency. This repository contains the XNU board
changes, standalone EFI applications and packaging scripts. ACPI and Windows
implementation files are not copied here. The XNU firmware profile omits ACPI
and selects the XNU loader as its default boot application. U-Boot is not used.

## Device support and implementation

Current installed checkpoint: **0100**. The owner confirms Settings opens on
0099;0100 logs verify D94 touch-profile selection. Icons/alert backing and a
SpringBoard assertion remain unresolved. The native installer rejects HFS
staging, so source-image application migration is incomplete. The0101 owned
publisher/native-cache comparison fix is compiled/tested but not installed.
See [current findings](docs/0100-userspace-findings.md) and the
[HFS/APFS comparison](docs/hfs-apfs-installation.md).

Earlier runtime checkpoint **0083**: the owner observed SpringBoard with fluid
animations, but touch input was absent. The kernel log shows a failed first
sensor probe and touch disabled throughout the run. **0084** adds bounded
deferred startup retries. Recovery logs verify successful sensor setup on
attempt 2 and delivery into backboardd; responsive UI input remains unverified. **0085** verifies all eight CPUs and three Edge clusters, but its shared
system-group override incorrectly preferred A75 for userspace services too.
**0086 is installed with exact BOOT readback verification**; it restricts
that override to IO-storage workers. M4 placement is observed in 0086, but the owner still reports no UI after
40 minutes. Startup performance remains unresolved. **Verified** means observed on this S10+, **Partial** means working with
known limits, and **Pending** means implemented/tested without runtime acceptance.
**0087 reached eight initialized CPUs but failed HFS root selection.**
[0088](docs/build-0088.md) fixes UFS DMA validation for the added owned RAM
banks; 0088 reached the lock screen. **[0089](docs/build-0089.md) is installed**
with exact BOOT readback verification and fixes current-format HID child-event
headers. Responsive touch and startup speed still require device evidence.
**[0090](docs/build-0090.md) is installed with exact BOOT readback verification.**
Its loader selects a SipHash-2-4 PACGA trial, and steady-state key reads no longer
take the global mutex. 0090 runtime confirms SipHash activation, but
the owner still reports no UI after80 minutes. **[0091](docs/build-0091.md) is installed with exact BOOT readback verification.**
It adds a bounded adjacent-PACGA pair trial and sampled timing; 0091 runtime confirms
5.2 million paired extra instructions, but sampled VM-query elapsed time still
dominates the wrapper. **[0092](docs/build-0092.md) is installed with exact BOOT readback verification.**
It replaces the expensive metadata query with a direct executable-leaf check;
The owner now reports an Apple logo at about1 minute and the lock screen at
about1.5 minutes after power-on (manual timings), and confirms working touch.
Fresh0092 samples reduce mean VM-query elapsed time from about2.08ms to4.92us.
The owner then reports a completely black screen after tapping Settings, with
no recovery from a short power-button press, and incorrectly drawn icons.
Native CoreImage render failures are present; the black-screen cause and
long-run stability remain unresolved.
**[0093](docs/build-0093.md) is installed with exact BOOT readback verification.** It corrects the proven zero IOSurface dimension
limits consumed by CoreImage CPU rendering and adds bounded presentation
progress counters. 0093 runtime no longer records the earlier CoreImage size errors, but the owner
still reports malformed icons and Settings black screen.
**[0094](docs/build-0094.md) is installed with exact BOOT readback verification.** It adds scoped native CoreGraphics backtraces to
identify the remaining NULL colorspace/context; it is a diagnostic trial.
Historical build notes below retain the status at the time of each experiment.

**[0095](docs/build-0095.md) is installed with exact BOOT readback verification.** It reports explicit unencrypted HFS
FileProtectionNone, completes the artwork device profile, and includes the owner's explicitly approved, bounded native
SystemAppMigrator bootstrap. Native app installation, PosterBoard migration and the visible graphics result await a fresh run.

**[0097](docs/build-0097.md) runtime identifies `No service cache` in launchd.**
The derived cache in 0096/0097 was shorter than the parent's fixed shared-memory
view. [0098](docs/build-0098.md) preserves the exact original length while adding
the one approved bootstrap job. Its production build and native XPC parsing
checks pass. [0098 runtime](docs/0098-userspace-findings.md) confirms service startup
and native iPhone artwork-profile regeneration. Its owned helper crashed in
dyld before main, and Settings triggered an IOGMD wire-count panic.
**[0099](docs/build-0099.md) is installed with exact BOOT readback verification
and reboot completed.** It balances descriptor prepare/complete at owned
adapter byte-copy sites and compiles the helper as arm64e. Its captured
runtime results follow below.

[0099 runtime](docs/0099-userspace-findings.md): the owner confirms Settings
opens. Icons and alert backing remain incorrect. The app helper now executes,
but native staging rejects the HFS root (APFS required, error166), and no apps
are installed. [0100](docs/build-0100.md) adds the missing D94 target used to
select the already-present UIKit EventTimingProfiles resources. **0100 is
installed with exact BOOT readback verification and reboot completed; D94
touch-profile selection is verified in its runtime log.**

[0100 logs](docs/0100-userspace-findings.md) now verify D94 touch-profile
selection. Rendering failures and a SpringBoard assertion remain. The cache
translated34 words in our owned helper, exposing an exact-byte publisher
mismatch; the0101 fix is compiled and tested but **not installed**. Native
migration still needs [HFS staging compatibility](docs/hfs-apfs-installation.md),
and the failed one-attempt receipt is preserved.

| Component | Device status | How it works / current limits |
| --- | --- | --- |
| Boot chain and XNU startup | Verified | S-Boot loads the packaged UEFI firmware. The embedded EFI loader reads XNU from the BOOT trailer, builds Apple `boot_args`/device tree and hands control to XNU. |
| CPU, scheduler and interrupts | Verified: 8/8 CPUs | Four Cortex-A55, two Cortex-A75 and two Samsung M4 cores run through the B2L platform driver, GICv2 and architected timers. Per-CPU timers and bidirectional SGI1/xcall checks pass; the kernel uses an explicit generic ARMv8 target. See [0082](docs/build-0082.md). [0085](docs/build-0085.md) adds A55/E, A75/M and M4/P clusters with QoS preferences; three-cluster startup is verified. [0086](docs/build-0086.md) corrects the shared system-group preference; runtime performance remains pending. |
| RAM | Partial: 6976MiB owned at0090 startup | [0087](docs/build-0087.md) retains the primary arena and adds separately owned conventional-memory spans. Fixed firmware/EL2/display reservations and physical gaps are excluded. The whitelist ceiling is about7045MiB;0090 reports6976MiB owned across13 spans and reaches userspace. Long-run allocator coverage remains incomplete. |
| Native UFS storage | Verified | XNU accesses the Exynos UFS controller directly after the EFI handoff, with GPT partition discovery, bounded reads/writes, flush and readback. Storage is not emulated by a host computer. |
| Root filesystem | Verified: read/write | The Apple OSS HFS driver mounts the journaled HFS root inside USERDATA and performs journal replay. This port uses HFS rather than native iOS APFS. |
| ARM64e userspace | Partial | Kernel traps emulate supported instructions missing on Exynos, including authenticated control flow and LDAPR. This allows real launchd, dyld and iOS services to execute; it does not provide hardware pointer-authentication security or universal instruction coverage. |
| Persistent native-code cache | Verified | Four workers learn eligible executed instructions and commit checked A/B records to a roughly 5.52 GiB SYSTEM cache file. A 1.5 MiB RAM hot window and disk index avoid keeping the whole file in RAM. On the next normal boot, XNU applies matching pages to the HFS root before launchd, archives undo data and reclaims applied cells. This is selective reuse, not a complete offline ARM64e conversion. See [cache lifecycle](docs/native-cache.md). |
| Display and SpringBoard UI | Partial; Settings opens on0099 | Native QuartzCore software rendering submits IOSurfaces; the B2L framebuffer path copies supported BGRA layers to physical scanout using the CPU. 0092 reaches the lock screen in about90s by owner timing and touch works. The owner reports malformed icons and a black screen after tapping Settings; CoreImage render failures are logged, but causality for the black screen is unresolved. See [CPU presentation](docs/build-0061.md). |
| Source-image application migration | Blocked: native staging requires APFS | The owned arm64e helper invokes the shipped SystemAppMigrator once;0099 finds50 candidates but installs0. MIStagingManager rejects the HFS volume with error166. A real HFS-compatible staging path and native desired-state handling remain required. |
| GPU acceleration | Not implemented | There is no working Mali graphics driver for this iOS stack. Visible frames currently use the CPU presentation path. |
| Touchscreen | Partial; touch accepted on0092 | Native sec_ts/HSI2C input reaches backboardd, which matches it as the main built-in digitizer. [0089](docs/build-0089.md) fixes current-format child framing. On0092 the owner confirms working touch, and the kernel records down/up events. Settings subsequently produces a black screen; sustained interaction remains unverified. |
| Physical buttons | Partial | GPIO input becomes HID consumer events for power/volume; Bixby currently maps to Home. The power-off overlay has been observed. Button events do not imply working audio or complete power management. |
| Kernel and iOS logs | Verified | Framed kernel records persist in a separate 16 MiB SYSTEM tail. Native tracev3, UUIDText, timesync and crash reports can be exported from recovery and decoded offline; redacted/missing strings and uncommitted panic-tail records remain possible. |
| AMFI, entitlements and containers | Partial | Scoped B2L development providers expose original identity/entitlement metadata and selected native API contracts. They do not establish Apple's production signing chain. 0083 adds exact pinned `extensions.plist` detached-data admission to target the ExtensionKit/DataMigrator retry storm; runtime acceptance is pending. |
| Sandbox / Photos access | Partial; 0083 fix pending | Development extension capabilities track issuer, target, consumer and lifetime. 0083 grows token/handle storage in chunks to address the observed ENOSPC/Photos failure. This does not implement full production sandbox enforcement or grant blanket TCC access. See [0083](docs/build-0083.md). |
| KeyStore / passcode state | Development only | A volatile no-passcode KeyStore supplies the limited startup and lock-state contracts used by this unencrypted HFS environment. It is not a SEP, hardware-backed key storage or encrypted iOS data protection. See [KeyStore scope](docs/development-keystore.md) and [lock-state correction](docs/build-0071.md). |
| Watchdog service | Partial | IOWatchdog accepts the supported userspace check-ins. Hardware watchdog reset/recovery is not implemented by this service. |
| Software product identity | Partial: native iPhone artwork profile verified on0098 | `hw.product`/`hw.machine` now advertise `iPhone17,2`; `/buttons/home-button-type=2` selects no physical Home button. Samsung driver matching is retained. Previously, the unknown product fell back to the iPod class. Native MobileGestalt now regenerates phone idiom, subtype2868, scale3 and iPhone16ProMax artwork metadata. The displayed identity still needs runtime confirmation. |
| System suspend / resume | Not implemented | Deep system sleep is disabled to avoid entering unsupported CPU halt/resume paths. Display blanking and interrupt-driven CPU idle are separate; battery life and sustained thermal behavior are not validated. |
| WiFi | Address metadata only | The loader privately supplies this device's real WiFi address to MobileGestalt, avoiding a known discovery timeout. There is no working WiFi networking driver in this iOS port; the address is not committed to the repository. |
| Bluetooth, cellular and audio | Not validated | No working Bluetooth, modem/call or speaker/microphone path is claimed. Installing an iPhone IPSW or advertising its product identity does not add these hardware drivers. |
| USB / ADB | Recovery only | Logs and BOOT updates use Android recovery's USB/ADB stack. A working iOS USB gadget/ADB connection is not provided by XNU. |

## Source baseline

- [XNU `rel/xnu-12377`](https://github.com/apple-oss-distributions/xnu/tree/rel/xnu-12377),
  pinned to `ac9718fb1af618d5ce8678d0dc6e8a58f252216f`.
- Explicit generic ARMv8-A with crypto instructions, PAN and 4 KiB pages.
  The current target enables all eight Exynos 9820 CPUs; 0082 device output and
  the recovered runtime log verify per-CPU timers and bidirectional SGI1.
- GICv2 at `0x10101000` / `0x10102000`; virtual timer INTID 27.
- Actual architected counter frequency read at runtime; expected 26 MHz.
- Apple PAC, BTI, SPTM and PPL are not used by this initial B2L target.

See [0082](docs/build-0082.md), [0083](docs/build-0083.md) and the
[automatic native cache](docs/native-cache.md) for the current CPU target,
latest fixes, read-only EFI staging and journal-safe pre-init code-page reuse.

## Build and package status

The standalone ARM64 kernel, debugging symbols, EFI loader and external 4 MiB
firmware profile build locally. Packaging produces a single Android v1
`boot.img` with the firmware and a bounded XNU payload trailer. The embedded
loader reads that payload through the existing firmware UFS service from GPT
partition `BOOT`; no separate ESP or SYSTEM/USERDATA transfer is required.

**Physical-device progress now includes native read/write HFS, iOS userspace,
visible SpringBoard UI and verified eight-core startup. Stable, responsive
home-screen use remains unverified.** The table above is the current support
snapshot; the chronological notes below document earlier bring-up milestones.

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
