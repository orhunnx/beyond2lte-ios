# Build 0026: ARM64e dyld page-in linking

## Device result

Build 0025 passes the previous malloc boot-identity check. The next observed fatal message is `fixup_page(): unknown pointer_format 12`. Format 12 is `DYLD_CHAINED_PTR_ARM64E_USERLAND24`, whose existing XNU implementation is compiled only for `HAS_APPLE_PAC`. B2L uses an ARM64 kernel without that hardware path, so the pager rejects these userland data pages.

## Correction

Patch 0014 compiles the existing authenticated page-fixup algorithms for B2L as well. A B2L-only signer implements the opt-in stripped-pointer ABI: decoded targets are stored raw and supported IA/DA key validation remains. It does not generate or claim hardware authentication. The native Apple PAC signer and native-board dispatch remain unchanged.

B2L dispatch returns the fixup result to the existing UPL failure handling instead of ignoring it. Supported ARM64e formats are 1, 9, 12 and 13. Bind ordinals, signed addends, image-relative and slide-relative rebases, chain bounds and malformed starts retain checks. Extra B2L checks require native physical page geometry, a valid page index and an aligned start before dereferencing a chain. `map_with_linking_np` already admits only native header page size; this change does not add 16 KiB logical dyld linking to a 4 KiB kernel, nor alter the separate mixed shared-cache slide pager.

## Verification

A synthetic harness extracts and compiles the actual dispatch, signer, speculation-bound delta check and both authenticated fixup algorithms. ASan/UBSan cases cover formats 1/9/12/13, authenticated and plain binds/rebases, a 24-bit ordinal above 65535, negative addends, a second segment page, no-fixup sentinel, invalid ordinal/key/start/delta/page geometry, disabled-mode gating and unsupported format. Native kernel compilation and fourteen-patch fresh/upgrade/idempotence/local-edit refusal checks pass. Exact kernel/dSYM UUIDs and package validation are recorded in ignored local output.

Only BOOT is updated. No userspace files, shared cache, USERDATA, SYSTEM, GPT or recovery are rewritten. Runtime continuation after the fixup boundary and complete launchd startup remain to be verified on the device.
