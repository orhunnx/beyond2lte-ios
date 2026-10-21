# B2L signing API inventory — unsigned development root

Read-only source snapshot, 2026-10-22 (Europe/Istanbul). Main repository HEAD
`718afcd`. Build 0029 subsequently adds the metadata-backed identity fix described below;
that change is not part of this inventory baseline.

## Scope and status terms

- **Implemented**: an actual source path exists for this development mode;
  this alone does not prove an iOS caller exercised it successfully.
- **Known failing**: a device screen shows a boot failure and the
  inspected source explains its errno.
- **Gap, call unobserved**: the unsigned development path lacks the metadata
  bridge; no supplied runtime evidence says it currently blocks boot.
- **Native behavior**: an upstream error/absence can be correct and should not
  automatically be changed to success.

Baseline: ARM64e development compatibility enabled, global signing enforcement
intentionally disabled, rootfs executable metadata available, no verified
`cs_blob`, and neither `CS_VALID` nor `CS_DEBUGGED` asserted. Different flags,
real attached signatures, other policies and malformed userspace arguments can
change the returns described below. Existing csops target/audit/MAC access
checks remain before all operation-specific branches.

Sources: the pinned prepared XNU tree and this repository.

## csops operations

| Operation | Current unsigned result / requirements | Classification | Native source |
|---|---|---|---|
| `CS_OPS_STATUS` **0** | Copies native flags, adding platform/path through their getters. Rootfs development metadata supplies platform classification; no signature-validity flags invented. | Implemented; signature verification is not claimed. | `bsd/kern/kern_proc.c:3784`; `bsd/kern/kern_cs.c:933`; `xnu-overlay/bsd/kern/b2l_amfi.c:66` |
| `CS_OPS_PIDOFFSET` **6** | Copies `p_textoff`; no valid-signature requirement. | Implemented native metadata query. | `kern_proc.c:3861` |
| `CS_OPS_ENTITLEMENTS_BLOB` **7** | Active B2L bridge copies owned original XML blob from credential metadata through `csops_copy_token`; missing XML returns `ENOENT`, short buffer `ERANGE`, bad pointers propagate faults. | Implemented XML-only development metadata. | `kern_proc.c:3907-3926`; `b2l_amfi.c:74` |
| `CS_OPS_IDENTITY` **11** | Current native branch rejects absence of `CS_VALID|CS_DEBUGGED` with **`EINVAL`** before calling `cs_identity_get`. That getter also requires `CS_SIGNED` and a real cs_blob. | **Known failing** in the 0028b launchd/boot-task observation; subsequently addressed in build 0029. | `kern_proc.c:4118-4148`; `kern_cs.c:1917` |
| `CS_OPS_TEAMID` **14** | Same validity gate yields `EINVAL`; real getter needs cs_blob, then returns its actual team ID. Even a legitimately signed Apple executable can have no Team ID, correctly yielding `ENOENT`. | Gap, call unobserved. Absence of a Team ID must remain an absence; do not derive one from filename/identifier. | `kern_proc.c:4118-4148`; `kern_cs.c:859` |
| `CS_OPS_CDHASH` **5** | Does not first require `CS_VALID`; calls `vn_getcdhash → ubc_cs_getcdhash`. No covering cs_blob gives **`EBADEXEC`**. Wrong output size gives `EINVAL`. | Gap, call unobserved. No actual CodeDirectory hash has been made available through the unsigned metadata bridge. | `kern_proc.c:3867`; `bsd/vfs/vfs_subr.c:3817`; `bsd/kern/ubc_subr.c:6197` |
| `CS_OPS_CDHASH_WITH_INFO` **18** | Same real-cs_blob dependency, returning actual hash plus hash type on success; otherwise `EBADEXEC` in this baseline. | Gap, call unobserved. The executable boot hash is not a CDHash substitute. | `kern_proc.c:3887` |
| `CS_OPS_DER_ENTITLEMENTS_BLOB` **16** | Initial validity gate yields **`EINVAL`**. With valid/debugged state it still needs a real cs_blob; B2L's DER/transmutation callback returns false and clears output. | Gap, call unobserved. XML support does not imply DER decoding or DER query support. | `kern_proc.c:3960`; `b2l_amfi.c:151` |
| `CS_OPS_BLOB` **10** | Initial validity gate yields **`EINVAL`**. `cs_blob_get` exposes actual attached signature bytes, not credential XML; no development fallback. | Gap, call unobserved; keep distinct from operation 7. | `kern_proc.c:4098`; `kern_cs.c:1946` |
| `CS_OPS_VALIDATION_CATEGORY` **17** | `csproc_get_validation_category` returns **`EINVAL`** without valid/debugged state; then `EBADEXEC` if no cs_blob. | Native authentication result unavailable; do not fabricate an Apple/platform validation category. | `kern_proc.c:4003`; `kern_cs.c:1058` |
| `CS_OPS_CLEAR_LV` **15** | Native non-macOS branch returns **`ENOTSUP`**. | Native behavior, not evidence of a missing B2L service or current boot blocker. | `kern_proc.c:4055` |
| `CS_OPS_CLEARPLATFORM` **13** | Development support clears native process/task platform status even without a cs_blob; immutable credential metadata cannot reassert it while flag is clear. | Implemented explicit state transition. | `kern_cs.c:955-979`; `kern_proc.c` clear-platform case |
| `CS_OPS_MARKHARD` **2**, `SET_STATUS` **9**, `MARKKILL` **3** | Harden/set operations require native valid state; `MARKHARD`/`SET_STATUS` can fail `EINVAL`; `MARKKILL` can actually kill an invalid process. | Native policy operations; **do not probe as harmless getters or blanket-return success**. | `kern_proc.c:3834-3859,4024-4053` |

## fcntl signature-facing operations

| Operation | Current behavior | Classification | Native source |
|---|---|---|---|
| `F_ADDFILESIGS_RETURN` **97** | Under disabled enforcement, B2L validates regular vnode/signature ranges and returns the **unsigned file extent**. It does not attach a signature, mark pages signed, or create a CDHash. | Implemented development cache-admission path; previous dyld/cache boot boundary was addressed here. | `bsd/kern/kern_descrip.c:4145-4169`; project `docs/build-0023.md` |
| `F_ADDSIGS` **59**, `F_ADDFILESIGS` **61**, `F_ADDFILESIGS_FOR_DYLD_SIM` **83**, `F_ADDFILESIGS_INFO` **103**, development `F_ADDSIGS_MAIN_BINARY` **113** | Apart from 97's special branch, native path reads/copies signature bytes and calls `ubc_cs_blob_add`. 103 returns actual attached cdhash/type when present. The metadata adapter's signature-context adjustment returns `KERN_NOT_SUPPORTED`; no successful general signature-admission path is established here. | Real-signature path remains; call/result unobserved for these variants. 83 also applies dyld-simulator restrictions. Do not assume all variants are covered by 97. | `kern_descrip.c:4081-4316`; `b2l_amfi.c:156-162` |
| `F_GETSIGSINFO` **105** | Supports only `GETSIGSINFO_PLATFORM_BINARY=1`. Direct `ubc_cs_blob_get` returns **`ENOENT`** when unsigned file has no cs_blob; unsupported info key returns `EINVAL`. It does **not** use the process development platform getter. | Gap, call unobserved. A plausible future platform-query mismatch, not an observed blocker. | `kern_descrip.c:4467-4515`; `bsd/sys/fcntl.h:610-622` |
| `F_GETCODEDIR` **72** | Explicit native **`ENOTSUP`**, regardless of B2L metadata. | Native unsupported API; declaration in header does not imply implementation. | `kern_descrip.c:4430` |
| `F_FINDSIGS` **78** | Same explicit native **`ENOTSUP`**. | Native unsupported API. | `kern_descrip.c:4431` |
| `F_CHECK_LV` **98** | Copies/validates the request and dispatches `mac_file_check_library_validation`. B2L's registered AMFI metadata policy supplies no file-library-validation hook. With no rejecting policy, valid requests fall through with zero; this is **not** a signature validation result. | Native policy plumbing present; no complete Apple library-validation service or current failure shown. | `kern_descrip.c:4435`; `security/mac_file.c:154`; `b2l_amfi.c:184-189` |
| `F_ADDFILESUPPL` **104** | Native supplemental-signature case exists only under `CONFIG_SUPPLEMENTAL_SIGNATURES`; no B2L development bridge inspected. | Conditional native feature; outside observed boot path. | `kern_descrip.c:4320` |

## Kernel process/file/vnode queries and AMFI callbacks

| Query surface | Current metadata behavior | Status / limit |
|---|---|---|
| OSEntitlements `*WithProc` Boolean/string/object queries | Retained target credential metadata; true Boolean only, exact strings/array members, typed objects. | Implemented at `b2l_amfi.c:132-146`; no real signature assertion. |
| `csproc_get_platform_binary`, new-task `task_get_platform_binary` | Explicit rootfs development policy flows into load result/task+VM flags; process getter observes label only while native platform flag remains set. | Implemented development classification, not Apple platform authentication. |
| `csproc_get_teamid`, `csproc_get_identity`, `cs_identity_get`, `cs_get_cdhash` | Still use real cs_blob and usually process `CS_SIGNED` gates. | Gap, call unobserved except the csops identity use being fixed separately. Getter fallbacks need lifetime/access design, not temporary strings. |
| `csfg_get_platform_binary`, `csfg_get_teamid`, `csfg_get_identity`; `csvnode_get_platform_identifier` | Direct file/vnode UBC signature state: **0/NULL** without attached cs_blob. Credential metadata does not populate these. | Gap, call unobserved. File platformness is distinct from process platformness. `kern_cs.c:1096,1430,1665,1718`. |
| `cs_entitlements_blob_get_vnode`, `cs_entitlements_dictionary_copy_vnode` | Valid vnode but no cs_blob gives success with **NULL/zero**, meaning no cached entitlements. | Gap, call unobserved; no vnode metadata reader wired to these APIs. `kern_cs.c:1809,1838`. |
| `cs_entitlements_blob_get(proc)`, `cs_entitlements_dictionary_copy(proc)` | Separate legacy process entrypoints still gate on `CS_SIGNED`; they do not route through the new `*WithProc` interface or csops XML branch. | Gap, call unobserved; 7's working bridge does not imply every legacy API is covered. `kern_cs.c:1868,1888`. |
| Opaque CoreEntitlements context queries, DER translation, profile exemptions, UDID/trust caches | Context conversion unavailable; DER/profile/UDID callbacks clear outputs and return false; signature context adjustment returns `KERN_NOT_SUPPORTED`; no trusted cache matches invented. | Intentionally unavailable. Their absence alone does not identify the next boot failure. See project `docs/development-amfi.md`. |
| Native entitlement-derived exec flags | Apart from explicit development platform classification, not inferred wholesale from the XML dictionary. | Distinct integration gap; do not claim complete A5 parity. Project `docs/development-amfi.md:115-118`. |

## A5 comparison

A5 `xnu/patches/0035-sweet-amfi-entitlements-platform-binaries.patch` attaches XML
and platform classification in `vnode_check_signature` to an actual cs_blob;
`0036-sweet-amfi-cs-signed.patch` propagates CS_SIGNED so process getters can see
it. That makes many native identity/team/hash/vnode APIs work through their
existing cs_blob routes, provided signature attachment/validation succeeded.

B2L deliberately skips the 16-KiB signature admission path and does not have
that attached object. Its credential-owned metadata adapter solves a different
contract. Copying only A5's CS_SIGNED/CS_VALID bits would make an unsupported
verification claim and would not safely materialize the missing blob.

## Priorities without fabricated success

1. Fix the **observed** operation 11 identity lookup by extracting the real,
   bounded CodeDirectory identifier as explicitly unverified metadata. Build 0029 implements this; do not use basename as identity.
2. If a later caller hits operation 14, return an actual embedded Team ID only
   when present; **ENOENT is legitimate** for an absent team field.
3. Treat future CDHash requirements separately: a real hash of the actual
   CodeDirectory bytes and actual hash type is metadata; the executable boot
   identity, a random hash or an executable-content digest is not a CDHash.
   Returning such metadata must not mark the executable verified.
4. File/vnode platform and XML/DER queries are the clearest remaining parity
   gaps. Wire only the observed required surface with explicit development
   semantics and correct ownership; do not emulate unrelated native failures
   as success.
5. A full verification route requires real signature loading/page-granule
   handling and actual validation, beyond this development metadata work.

## Snapshot identity

- native `kern_proc.c` SHA-256: `6d3444a0db897bdd49f726df7146df8a12f38e16d6902b32633dfbd2810860c1`
- native `kern_descrip.c`: `6ded5fe6d6b4f927752a8c9d1301858528d08994a66e7e7d637d4f6e2c0eda13`
- project `b2l_amfi.c`: `4a1c4752927a4e8c6566c999ba92a583e79a35f216a2f3a487c3999618b2a28c`
- project `b2l_amfi_metadata.c`: `1f63ce62734ef668e1433d7dac9f7e0cd836bbb3108eed6cc7bfb73602f92bc6`

This is a bounded static source inventory, not an all-iOS API audit or a runtime
call trace. Only the device-observed identity failure is classified as a newly
observed blocker. The inventory itself used read-only inspection.
