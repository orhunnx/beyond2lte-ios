# Build 0022: native PAN management for B2L copyio

## Observed progression

- 0017 enabled the existing `cs_enforcement_disable=1` development boot option.
- 0018 skips Mach-O signature attachment and the trusted-dyld requirement on B2L only when global signature enforcement is disabled. No verified signature or entitlement is fabricated.
- 0019 records bounded exec, EL0 exception, emulation and process-cleanup events through the direct framebuffer sink.
- 0020 showed the first undefined-instruction handler stopping inside generic `copyin` before the compatibility wrapper.
- 0021 uses the fault-recoverable aligned `copyin_atomic32` path for instruction reads. The device shows address/kernel-buffer validation returning zero, followed by the scalar-load begin marker without its return marker.

The device has successfully loaded launchd/dyld and reached EL0. The first observed undefined instruction is dyld's `RETAB` in `mach_continuous_time`, at file offset `0x8fd04`. The decoder already implements this opcode. The subsequent 0022 device photo shows dyld/libignition executing beyond this boundary and reaching shared-cache signature registration. This confirms continuation past the copyio stall; it does not establish complete instruction compatibility.

`B2L EXIT: init[1] status=0` also occurs during exec's old-process cleanup; this message alone is not evidence that the newly loaded launchd exited or crashed.

## Correction

The B2L board had `__ARM_PAN_AVAILABLE__=0`. Exynos 9820 implements PAN, and exception entry with SCTLR_EL1.SPAN clear can set PSTATE.PAN. Compiling out native PAN management leaves checked copyio's privileged loads unable to access EL0 mappings. This matches the device's load-boundary stall; device continuation after the correction is required to confirm causality. The stock B2L SCTLR initialization leaves SPAN clear. See the [Arm PAN specification](https://documentation-service.arm.com/static/649b0eac38511951cb7993e1), section B5.1, for the privileged-data-access fault behavior.

B2L now enables the existing XNU PAN implementation. Stock initialization establishes PAN, checked copyio temporarily clears it for access, and restores it afterward. Context-switch checks and saved kernel state use the same native PAN contract. No physical RAM range, reserved region, page permission or signature-policy setting is changed by this correction.

The additional hardware capability probe was removed at the user's request. PAN support is a B2L platform requirement. The bounded scalar-load messages report the existing PAN state before and after access.

## Local verification and device evidence

Full kernel build, exact kernel/dSYM UUID matching, native disassembly of checked copyio's PAN transitions, package bounds/embedded-loader validation and full BOOT readback are recorded with the ignored local artifacts. Build/install completion and actual successful EL0 emulation must be reported separately.

Local binaries, symbols, photos, identifiers, seeds and installation receipts remain outside Git. No USERDATA or SYSTEM rewrite is needed.

## Subsequent device result

Build 0022 progressed to dyld/libignition output. The observed fatal error is failed shared-cache code-signature registration, followed by missing `libSystem.B.dylib` because the cache was not loaded. The separate preboot lookup error is 25 on the unsealed HFS development root. No complete launchd service startup or UI is claimed.
