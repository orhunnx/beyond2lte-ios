# Build 0027: missing sandbox container query

## Device evidence

Build 0026 passes the previous format-12 fixup error and reaches `_container_init`, which aborts because `sandbox_get_container_expected()` returns -1. The iOS 26 libsystem_sandbox wrapper invokes MAC policy `Sandbox`, operation `0x43`. It supplies a two-pointer argument block whose members address two 64-bit local outputs. Any nonzero syscall result becomes -1; success converts each output into a boolean. The older A5 shim returns ENOENT for this operation, which is insufficient for this userspace version.

## Narrow development fallback

Patch 0015 adds a fallback only when no static or dynamic policy handled the syscall, the target is exactly `Sandbox`, the operation is `0x43`, the caller is 64-bit, ARM64e development compatibility is enabled, and global process signing enforcement is disabled. It copies in the two-pointer argument block and copies out two zero 64-bit values, representing no expected container in the unconfined development port. Null output pointers are optional; copyin/copyout errors propagate.

Existing MAC syscall authorization is checked first. Real policy results take precedence, including a real provider returning ENOPOLICY. Other policy names, operation numbers, 32-bit callers and enforcement-enabled operation remain unchanged. This is not a Sandbox implementation: it does not supply profiles, paths, extension tokens or security isolation, and does not grant blanket success to unknown operations.

## Verification

The test extracts both the actual fallback helper and complete MAC syscall dispatch. ASan/UBSan scenarios cover output initialization, disabled compatibility, enabled enforcement, 32-bit callers, wrong operation/name, authorization failure, argument/output copy failures, optional outputs, and static/dynamic real-policy precedence with success, EPERM and ENOPOLICY. Native compilation, package validation and matching stripped/unstripped/dSYM UUIDs pass; the fifteen-patch sequence passes fresh/upgrade/idempotence/local-edit refusal.

Only BOOT is installed. Local binary inspection artifacts, symbols, identifiers and receipts remain outside Git. USERDATA, SYSTEM, GPT and recovery are unchanged. Runtime continuation after container initialization and full launchd service startup require subsequent device evidence. The separate preboot lookup error remains unresolved.
