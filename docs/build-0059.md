# Build 0059: framebuffer query ABI corrections

This build deliberately retains the existing AMFI implementation. The separate
operation 0x66 proposal is not compiled into it while exact-scope approval is
pending. Device installation does not establish that backboardd's AMFI assertion
is resolved.

## Native framebuffer calls

Implement the inspected iOS 26.6.2 wrapper contracts:

- Selector 74: one scalar index input, two scalar outputs and a 64-byte name
  buffer. The fixed CPU scanout reports zero frame-info descriptors, which the
  native enumeration loop accepts. It does not invent display profile data.
- Selector 88: selects logical display type 0 (primary). Other types remain
  unsupported because this service exposes only the built-in scanout.
- Selector 84: accepts identity contrast 1.0 using its exact 32-bit bit pattern.
  Other values remain unsupported; no pixel-transform capability is claimed.
- Selectors 68 and 79 remain unsupported. At most 24 bounded value/input-prefix
  diagnostics record their parameter/block identity for later ABI investigation.

Malformed new requests return before output mutation. The actual dispatch
cases pass ASan/UBSan buffer, count, descriptor, guard-byte and invalid-input
tests. Existing graphics boundary tests pass. The source is synchronized into
the prepared XNU tree, and the DEVELOPMENT ARM64 B2L kernel builds with the old
AMFI source hash explicitly verified. Firmware, touch support and reservations
are unchanged from 0058.

A complete BOOT prefix readback verifies installation. Main-display discovery,
render-server availability, successful backboardd startup and first-frame
presentation still require runtime evidence.
