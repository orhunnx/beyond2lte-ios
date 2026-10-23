# Build 0062: IOSurface metadata contracts

This candidate adds native iOS 26 IOSurface selectors used during CPU frame
setup. It includes the corrected 0061 framebuffer masks. It is prepared for a
subsequent device trial; 0061 remains the installed isolated mask test.

- Selector 5 stores a YCbCr matrix code in shared state +0x6e. Byte codes are
  supported; values above 255 are rejected because native truncation is unproven.
- Selector 27 validates its 160-byte request, bounded attachment length and all
  22 changed-field spans before updating shared attachments at +0x34.
- Selector 9 decodes the serialized value/key or dictionary update into retained
  per-surface properties. A serialized readback snapshot and shared length/seed
  are published only after allocation, serialization and seed-output success.
- Selector 10 returns that dictionary snapshot with the native 12-byte header.

Metadata operations require a surface already held by the requesting client.
Input snapshots and serialized dictionaries are bounded to 64 KiB; collection
references that could form retain cycles are rejected. Other unsupported
operations retain explicit errors. Short output-descriptor writes now report
failure, and lock-result allocation failure releases its retained client map.

The actual adapter helpers pass ASan/UBSan tests covering matrix byte codes,
all attachment spans, ownership, dictionary merge/readback, seed publication,
allocation/copy failures and malformed streams. Native OS collections and
serialization APIs are modeled in the host harness; the real XNU build checks
those API calls. These checks do not establish a visible frame or full UI.
Private Apple binaries and device evidence stay outside the repository.

## Repeated Mach-port lookup lifetime

Like A5's cached-lookup correction, selector 34 returns base UINT64_MAX and
the surface ID when this client already owns the mapping. The shipped iOS 26
client recognizes that sentinel, reuses its cached userspace object and adds
only a userspace retain. Existing and raced kernel mappings therefore do not
gain a second mapping count. Normal new lookups still map and count once.
A failed cached reply preserves the original mapping; only counts actually
added by a failed request are unwound. A separate actual-function sanitizer
harness covers existing/raced publication, both reply modes, mapping/array
allocation failures and short outputs.

The real XNU DEVELOPMENT ARM64 B2L build passes with these changes. This
candidate is packaged offline. The user has confirmed an Apple logo on the
installed 0061 mask-only trial, which remains running for observation.
