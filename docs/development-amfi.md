# Development AMFI metadata adapter

This adapter is active only when `b2l_arm64e_compat=1` and global signature
enforcement is disabled. It preserves any preexisting AMFI provider. It does
**not** claim that an executable, its XML, or its CodeDirectory is verified.
No `cs_blob` is fabricated or attached, no VM object is marked signed, and
`CS_VALID`, `CS_SIGNED`, page hashes and trust-cache results are not granted.

## Comparison with the A5 implementation

The A5 built-in AMFI interface and `SweetEntitlements` class are read-only design
references. A5 attached XML to a real, validated `cs_blob` and obtained process
metadata through `csproc_get_blob()`. The current B2L unsigned loader deliberately
does not create that object, so copying A5's `CS_SIGNED` logic would be incorrect.
B2L instead reads XML as development metadata and owns it through a native MAC
credential label. No Apple/private source, input XML or signature bytes are
included in the repository.

A local read-only inspection found XML metadata in the supplied launchd image:
2,142 XML bytes, 29 keys, with boolean and array values. That observation is not
a validation result or repository fixture. DER-only executables receive no XML
entitlements from this adapter.

## Bounded extraction and typed queries

The portable reader validates thin ARM64 executable headers or the selected
fat slice. It bounds command-table bytes to 64 KiB, commands to 4,096, signature
bytes to 2 MiB, signature entries to 64 and XML blob bytes to 64 KiB. Arithmetic
is checked before adding the slice offset. Malformed ranges, overlapping blobs,
duplicate XML slots, short reads and absent/DER-only XML produce no entitlement
values. The reader does not hash the XML or compare it with a CodeDirectory.

XML preflight rejects embedded NULs, alias attributes, internal DTD subsets,
more than 4,096 tags and container depth greater than 32. The public XNU
`OSUnserializeXML` then requires a dictionary and checks XML syntax and duplicate
keys. Missing, malformed, unsupported or allocation-failed XML yields an empty
metadata dictionary. Collections are recursively immutable before publication.

Supported OSEntitlements interface version 2 operations are:

- Boolean: only an actual boolean `true` grants the query.
- String: an exact string match, or a string member of an array.
- Object/integer/dictionary: ordinary retained OSObjects, with caller ownership.
- XML: a separate allocated blob copy, freed by the receiving kernel caller.

Unavailable CoreEntitlements opaque queries, DER translation and trust caches
remain unavailable. No fake opaque context/error object is constructed. Context
conversion returns NULL; DER/profile/UDID callbacks return false and clear
outputs. Signature-context adjustment returns `KERN_NOT_SUPPORTED`, so this
provider cannot turn unverified bytes into validated signature state.

## Credential lifetime

The MAC policy owns one object reference per credential label. Credential
association retains the immutable object; label destruction releases it. Fork
may share the already retained credential or receive a correctly associated
copy. Every exec requests a fresh credential transition, clears inherited
metadata and installs the actual main executable's metadata in the new model.
Scripts do not supply an independently trusted entitlement source.

Queries reference the target process's credential, retain its object while that
credential is alive, and then release the credential. A concurrent credential
replacement cannot leave a dangling object. Returned object/XML copies outlive
the original credential as specified. Failed exec cleanup releases the new
credential's references; no PID-indexed global map or stale text-vnode sidecar
exists. Allocation failure that prevents creating a rootfs metadata object
fails exec rather than publishing inconsistent task/credential classification.
The policy leaves other MAC policies' shared `disjoint` output unchanged.

## Explicit development platform policy

Regular main executables on the root filesystem are classified as development
platform binaries while the adapter is active. This decision is independent of
XML availability and is **not a cryptographic platform identity assertion**.
Other mounts, nonregular files and a disabled adapter receive no classification.
The decision enters `load_result.platform_binary` before runtime mitigations and
flows through XNU's existing new-task platform set/clear branch. Credential
transition applies only `CS_PLATFORM_BINARY`; it does not set validity/signature
flags. `csproc_get_platform_binary()` accepts the metadata only while that native
process flag remains set.

`CS_OPS_CLEARPLATFORM` clears the process flag and the actual task flag even when
no cs_blob exists. The immutable credential metadata cannot reassert it. A later
rootfs exec can receive a fresh classification; a nonplatform exec clears it.
The original csops access checks remain in place. `CS_OPS_ENTITLEMENTS_BLOB`
returns metadata XML without requiring a fabricated `CS_VALID` or `CS_SIGNED`;
DER and unrelated code-signing operations keep their existing behavior.

This policy is appropriate only for the deliberately unsigned development root.
A process able to change rootfs executables can also change their metadata; this
adapter provides no authenticity boundary against such changes.

## Verification

Run `bash tests/test-development-amfi.sh`. It uses generated synthetic inputs:

1. Actual reader source under ASan/UBSan: thin/fat bounds, overlap, missing XML,
   read errors, uint64 overflow and 20,000 bounded mutations.
2. Actual C++ adapter under ASan/UBSan with mock OSObject factory boundaries:
   typed queries, retained object/XML ownership, recursively immutable children,
   XML preflight, wrong dictionary type and allocation cleanup. The host harness
   does not claim to reimplement or retest the kernel's XML parser.
3. Actual C lifecycle/registration functions under ASan/UBSan: existing-provider
   preservation, opt-in/enforcement gates, credential association, exec model
   cleanup, exit, concurrent credential replacement, platform clearing/re-exec,
   allocation failure and preservation of another policy's disjoint decision.

All three new native translation units and the patched csproc/csops/exec units
also compile with the pinned XNU ARM64 development flags. These checks do not
establish device userspace success, activation, a complete Apple AMFI service,
or 16 KiB CodeDirectory validation. That signature loader remains bypassed in
the existing unsigned development path.

Vnode-only entitlement queries that depend on a real cs_blob still have no
metadata bridge in this build. Credential/process queries and csops XML are
the implemented paths; this is not complete API parity with A5. Native
entitlement-derived exec flags beyond the explicit platform classification
are not inferred from the development dictionary.

## Registration phase correction (0028b)

The interface initializes at EARLY_BOOT. Patch 0018 registers both MAC policies
in mac_policy_initmach, after security extensions and before mac_late and
machine lockdown. The iOS policy registry is SECURITY_READ_ONLY_LATE; registering
a policy in bsd_init is too late even though the API accepts late registration
on other targets. Build 0028 physically exposed that ordering error. No memory
protections are relaxed by the fix. `tests/test_policy_startup.py` checks this
ordering against the actual pinned startup graph.
