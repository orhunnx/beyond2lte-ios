# Build 0069: preserve native anonymous forward connection provenance

## Device-confirmed cause

0068 epoch36 has3,222,011 console bytes from sequence0, no missing positions or producer loss. It records106 successful constructions and35 validations:34 fail with service-only labels/result4 at callback stage5; one same-parent named validation succeeds.

Two objects match native anonymous forward construction in SpringBoard to rejection in backboardd. Raw log4911 constructs port key0x300d1cdaa83749b, flags0x283f, parent0xfffffffe, type5, label0; raw log4919 validates that same receive-right object in backboardd with an existing service label and no connection label, returning4. A second pair appears at4944/4951 for port key0x300d1cdaa8381bb. These task-local receive names differ across transfer; matching uses the obfuscated underlying object key and the latest preceding construction. Object keys may be reused after destruction, so unrelated older uses are not conflated.

Native libxpc deliberately supplies MPO_ANONYMOUS_SERVICE(-2) for endpoint-created channels. XNU deliberately skips named-label derivation for that sentinel. The B2L0056 strict non-NULL named-parent assumption consequently rejected a legitimate native endpoint path and contributed to the observed check-in/reconnect loops. This identifies a real defect; it does not establish that every GUI blocker is resolved.

## Narrow correction

B2L labels now distinguish named-service identity and anonymous-forward kind. Patch0046 hooks native mach_port_construct before ipc_port_alloc, only for IOT_CONNECTION_PORT, parentMPO_ANONYMOUS_SERVICE, and no MPO_IMMOVABLE_RECEIVE. The helper rechecks flags/type/parent and only allocates when the B2L development gate is active AND B2L successfully owns the native filter callbacks. Otherwise it returns success/NULL and leaves the original path unchanged. A failed repeated registration does not clear existing ownership. The public helper header is included in the native arm64 export list.

The provider accepts a valid named service with either its identical named parent label or an owned anonymous-forward label. NULL labels, wrong kind, different named parents and backward channels remain rejected. Anonymous kind does not fabricate a named parent relationship. Native receive-right/type checks remain before the callback. Native ipc_port_alloc owns the label and frees it on allocation failure; callback reference transfers and teardown remain balanced. Message-filter marking still follows MPO_FILTER_MSG. No signing or entitlement changes are included.

0068 bounded construct/validation diagnostics remain enabled. A corrected anonymous construction should have a nonzero label key; its validation can show different service/connection keys with result0 because anonymous kind is intentionally distinct from named identity.

## Verification

- Actual provider passes ASan/UBSan for owned-provider gate, exact anonymous sentinel, forward/type checks, allocation failure, NULL/wrong kind/mismatched named rejection, reference balance, repeated registration and eight-thread named and anonymous reference cycles.
- Actual patched native constructor fragment plus actual provider passes ASan/UBSan for anonymous-only allocation, backward/wrong type exclusion, named derive behavior, inactive foreign-provider pass-through, filter marking and both allocation-failure cleanup paths.
- Real DEVELOPMENT ARM64 B2L kernel builds. Disassembly of the packaged kernel's native mach_port_construct confirms the new helper call, preventing an unused-symbol/stale-object claim.

## Installation

All0068 logs were preserved first:552 files/775,334,724 bytes, all_selected_complete=true, including native tracev3/uuidtext/timesync and crash reports. A full previous BOOT backup matches0068.

0069 BOOT23,373,768 bytes, SHA256:
3f5d60fdd3bd6540f4cb46cf3084c06af55cb7c0d497d10e55cc5214bb43e1d4.

Firmware/loader prefix matches0068 byte for byte. Rooted beyond2lte recovery serialR58M33609TR and BOOT capacity57,671,680 checked; only BOOT written. Readback matches exact image hash and reboot was requested. Receipts/symbols/backups stay in ignored out/0069.

Installation is verified. Runtime result0, loop reduction and UI progress require the next device observation.
