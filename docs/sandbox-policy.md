# B2L registered development Sandbox policy

`b2l_sandbox.c` registers the policy name `Sandbox`, so normal XNU MAC syscall dispatch reaches a provider rather than returning ENOPOLICY for every operation. It follows the unconfined A5 development behavior, with the verified iOS 26 operation 0x43 response retained.

## Activation and startup

Patch 0016 adds the source to the B2L build. Patch 0018 places `b2l_sandbox_register()` and AMFI policy registration in `mac_policy_initmach()`, before `mac_late` and machine lockdown. The original 0028 BSD-time placement faulted on the iOS read-only policy list and is superseded. Registration occurs only when both conditions hold:

- `b2l_arm64e_compat_enabled()` is true.
- Global code-signature enforcement is disabled.

Callbacks recheck that gate. The syscall provider also requires a 64-bit process, matching the verified user ABI. Disabled gates perform no registration. The permanent built-in provider has no NOTLATE/UNLOADOK flags; an existing policy of the same name wins via the MAC framework's EEXIST result. Successful repeated initialization does not register twice.

Existing patch 0015's op 0x43 fallback is retained. Normal registered-policy dispatch takes precedence, so existing real policy errors are not converted into successful fallback results.

## Behavior

| Entry | Development result |
| --- | --- |
| Sandbox operation 4 | ENOENT; no fabricated container path |
| Sandbox operations 5/6 | ENOENT; no fabricated extension token/state |
| Sandbox operation 0x43 | Copy in two 64-bit output pointers; write 64-bit zero to each non-NULL output; return copyio error or success |
| Other Sandbox operations | Success, matching the requested unconfined A5 default |
| PRIV_WORK_INTERVAL (`1008`) | Grant through `mpo_priv_grant` |
| Other privilege grants | EPERM, allowing normal root/other-policy composition |

No MAC privilege-check hook is installed. An existing restricting policy still wins through `priv_check_cred`; a grant does not override a MAC denial. Root's normal privilege behavior remains in XNU.

The op 0x43 pointer pair is snapshotted before output writes, including overlapping input/output locations. Copyout follows ordinary partial-write semantics: a later output fault may occur after the first zero has been written. No process names, IDs or output data are logged. Atomic first-use logging is bounded to 129 operation lines and 65 privilege lines per boot, plus initialization status.

## Verification

```sh
python3 tests/test_registered_sandbox.py /path/to/prepared/xnu /path/to/xnu-overlay
```

The first test compiles the actual provider source, actual XNU `__mac_syscall` dispatch and actual `priv_check_cred` composition under ASan/UBSan, with bounded synthetic copyio and MAC registration models. It covers all 128 operation numbers, explicit A5 defaults, op 0x43 NULL/overlapping outputs and faults, disabled/enforced gates, 32-bit rejection, real-policy precedence, dynamic-list cleanup, registration errors, privilege restriction/root composition, and concurrent atomic logging across 16 threads.

The patch-sequence test reconstructs the pinned upstream files, applies the complete sequence, verifies upgrade/idempotence and refuses unrelated edits. Native translation-unit compilation against the existing XNU 12377 build headers/arguments also passed; this is not a full kernel build or device runtime confirmation. No Apple closed binaries are included.
