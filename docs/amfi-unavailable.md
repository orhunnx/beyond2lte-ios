# Unavailable AMFI entitlement interface

Build 0013 physically mounted the native HFS SYSTEM volume read/write and
completed native writes/flushes. Startup then faulted in the inlined
`IOCurrentTaskHasEntitlement` path because `amfi` was NULL. The next change is
`patches/0004-b2l-unavailable-amfi-entitlements.patch`; it provides unavailable
provider handling, not an AMFI implementation.

## Query contract

B2L's IOTask entitlement helpers and `IOUserClient::copyClientEntitlement` check
the provider, current OSEntitlements interface version and exact callback before
dispatch. An absent/stale provider or missing callback returns false/NULL.
Object-copy success without an object also fails closed. Integer and Boolean
getter outputs are initialized to zero/false on failure. The combined
Boolean-or-object query can use whichever supported callback is present; a
false Boolean object does not grant the entitlement.

Vnode queries use the existing csblob dictionary bridge. Without its real AMFI
dictionary callback there is no dictionary or entitlement object to return.
Existing real-provider values and object ownership are preserved. No XML parser,
fallback entitlement dictionary, privileged task exception or successful
authentication result is fabricated.

## Adjacent dispatches

Entitlement-context acceleration returns `EPERM` when its required provider or
adjustment callback is unavailable. It does not report a successful signed exec
without an entitlement provider. Invalidation/cleanup avoids missing callback
dispatches while retaining actual object release and code-signing flag changes.
The csops XML/transmuted-blob paths report an unavailable callback rather than
calling NULL. Optional MTE-policy exceptions are false when AMFI is absent.
Trust-cache initialization/query handling remains the earlier separate
fail-closed implementation; no provider table is registered by this patch.

## Verification

```sh
python3 tests/test_amfi_unavailable.py /path/to/prepared/xnu
```

The harness compiles the actual patched query/copy/vnode helper bodies and
entitlement-context acceleration under ASan/UBSan. It covers NULL, stale and
partial provider fixtures; malformed success without an object; negative output
defaults; real Boolean/string/integer values; object reference release; and
unavailable, legacy and current adjustment dispatch. The fixtures model only
these source contracts, not a production AMFI provider or userspace boot.
The full standalone 0014 kernel and dSYM build passed. The subsequent 0014 photograph confirms progress beyond the 0013 fault to
loading `/sbin/launchd`; the final ENOENT matches the empty root.
