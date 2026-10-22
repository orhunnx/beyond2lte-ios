# A5 comparison at B2L build 0051

Read-only source comparison, 2026-10-23. B2L source HEAD e49048d; A5 reference
checkout HEAD 506775feb4e06cfb0c2b1278324560fc2236dd94. A5 reference is
`experiments/ios-a5xelte` within the local sma510fpmos workspace. Hardware source
and Apple binaries are not copied by this review. This is a focused comparison
of current boot/container/display contracts, not proof of parity across every
historical patch or syscall.

A5 uses XNU 8796 / iOS 16; B2L uses XNU 12377 / iOS 26. Patch numbers and
filenames cannot establish whether an equivalent newer native API exists.
The earlier shim-audit and signing-api-inventory documents describe historical
baselines; their CPU, cache and DER/CDHash rows are not current 0051 status.

## Concrete differences

| Area | A5 source | Current B2L | Evidence and priority |
| --- | --- | --- | --- |
| Atomic regular-file replacement on HFS | 0052 plus A5 guard 0117: on ENOTSUP and exact VFS_RENAME_SWAP, two regular files on the same mount fall back to VNOP_EXCHANGE | No equivalent fallback in prepared kpi_vfs.c; staged hfs_vnop_renamex accepts EXCL, rejects SWAP | Relevant filesystem mismatch to investigate first for container/persona persistence. 0051's visible dirs_cleaner swaps involve directories, so this regular-file fallback would not fix those lines. No direct failed regular-file swap from the current MCM request is captured. |
| Image4 algorithm description | tools/mkadt.py:93 supplies /chosen/crypto-hash-method = sha2-384 | DeviceTree.c has no crypto-hash-method | Direct match to current cryptexd fatal. Actual iOS 26 libimage4 requests this property and accepts sha1 or sha2-384. Algorithm selection alone supplies no manifest, signature or Apple identity. |
| Display and shared surfaces | 0046 defines IOCoreSurfaceRoot, IOSurfaceRootUserClient, IOMobileFramebuffer and AppleCLCD; 0109 adapts scanout to A5 DECON and publishes the display match | GOP/boot framebuffer console exists, but no equivalent B2L IOSurface/IOMobileFramebuffer provider or display user client | A substantial missing prerequisite for real iOS graphics. CPU framebuffer presentation can be a separate milestone from full GPU support. A5's iOS 16 selectors, hardcoded address and 1080x1920 stride cannot be used unchanged for iOS 26/Exynos9820. |
| Display geometry | mkadt.py:98 supplies display-scale; display bridge returns pixel dimensions and density | No display-scale property or corresponding mobile framebuffer API | Missing display description; verify iOS 26 consumers and actual panel geometry with the provider implementation, rather than adopting an iPhone identity. |
| HID/input | 0058 service scaffolding; A5 0118/0123 button and touch adaptation | No B2L HID event provider | Needed for input after graphics. Exynos7580 GPIO/I2C and touch-controller code is device-specific. |
| AMFI object integration | 0035/0036/0106 attach XML and rootfs platform classification to native cs_blob paths and propagate process CS_SIGNED | Credential-owned XML/DER/identifier/CDHash metadata with narrow csops query bridges; internal flags/blob remain native | File/vnode and legacy blob-dependent queries still differ. A5 0107 also relaxes signed page/cache behavior for its 4 KiB lab kernel: attached cs_blob does not establish a complete Apple trust chain. The approved 0051 query bit does not materialize a native blob or close all these interfaces. |
| Entitlement diagnosis | 0116 traces parsed XML counts and system-container entitlement for backboardd and containermanagerd | Generic policy registration and bounded status samples; no equivalent targeted exec-metadata trace | Useful next diagnostic for current container failure: confirm actual target XML/DER availability and expected entitlement values, then capture MCM request/error. It is instrumentation, not a fix. |
| GPU/Metal | Later A5 0125..0192 work contains T720-specific hardware and private provider/compiler paths | No corresponding B2L Mali/GPU provider | Separate large porting effort. A5's progress notes distinguish bounded tests from full compositor support; the patches cannot establish S10+ acceleration. |

## Closely matching A5 history

A5XELTE_PROGRESS.md:95-101 records the following sequence:

1. backboardd received a nil system-container path; lsd also aborted in
   CoreServices' systemGroupContainerURL path.
2. Diagnostic 0116 found the expected backboardd system-container entitlement
   and the distinct container-manager entitlement of containermanagerd. Missing
   XML was therefore not a sufficient explanation of that A5 failure.
3. usermanagerd failed atomic regular-file writes of usersession.kb and
   persona.kb with ENOTSUP. 0117 enabled the existing regular-file exchange
   fallback for the A5 board.
4. The next phone observation showed an Apple logo. Recovered logs recorded
   BGRA swaps and display on/off; the saved window no longer showed the earlier
   usermanagerd ENOTSUP or nil-URL exception. Home-screen observation followed
   in the 0118 run.

This historical sequence makes the missing HFS fallback a strong comparison
lead. It does not prove that B2L's current MCM failure has the same cause or
that a regular-file data exchange satisfies every rename-swap caller. Open
file descriptors, inode identity, metadata, locks and journal crash behavior
need explicit consideration before adapting that workaround. Directory swaps
are outside its scope.

## Existing B2L counterparts

Native HFS/root writes and persistent console storage, actual pthread provider,
portable crypto, registered development Sandbox, work-interval privilege,
AppleKeyStore development interface, IOWatchdog discovery, and SMP have B2L
implementations. Current device evidence verifies six CPUs and four cache
workers; those are not missing A5 patches. The iOS 26 Sandbox operation 0x43
contract is intentionally different from A5's older ENOENT response.

B2L's ARM64e fixups/emulation and durable instruction cache address a different
userspace ISA problem. They cannot provide the missing display/HID services or
replace native filesystem and entitlement APIs.

## Next steps supported by this comparison

1. Capture the exact MCM client request/error and targeted exec entitlement
   metadata; do not infer client success from containermanagerd's self-status.
2. Adapt and verify the bounded HFS regular-file operation if that contract is
   needed; preserve errors for unsupported directories and other operations.
3. Add the verified Image4 algorithm description and check the next cryptexd
   boundary without inventing Apple device identity or trust results.
4. Implement a bounded iOS 26 IOSurface/mobile-framebuffer path using the actual
   S10+ framebuffer, then validate real submitted frame presentation. HID and
   GPU acceleration remain separate device milestones.

No device state was changed by this audit. No patch was installed.
