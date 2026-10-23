# Build 0060: consistent unavailable AMFI launch-constraint response

The user explicitly approved the exact persistent development-policy scope
recorded in [amfi-constraint-response-proposal.md](amfi-constraint-response-proposal.md).
The existing unsigned-development gate and metadata credential policy remain.
After that metadata policy registers, a separate permanent MAC policy named
AMFI registers only if no policy of that name already exists.

## Operation 0x66

The callback validates the 64-byte argument layout, 16 KiB maximum constraint,
64-bit input range and exact 1028-byte result capacity. User copies remain
faultable; input is copied in bounded chunks without interpreting predicates.
For a structurally valid request it copies an initialized result with code 3
and an explicit description that matching is unavailable and was not evaluated,
then returns ENOTSUP. This keeps the userspace false return consistent with a
nonzero result error field. It never reports a successful match.

When output capacity is valid, malformed/unreadable constraint inputs receive
stock-style result code 5 together with EINVAL/EFAULT; copyout failure takes
precedence. An unreadable outer argument or invalid output capacity cannot
receive a result copy. Other operations remain unsupported with ENOTSUP;
previously an absent policy returned ENOPOLICY. Disabled development mode or a
non-64-bit process returns ENOPOLICY. A preexisting real AMFI policy is retained.
No signing flags, entitlement contents, signature validity, trust caches or
process identity are changed.

## Verification and runtime boundary

The actual callback passes ASan/UBSan tests for layout/offsets, size boundaries,
initialized failure output, every copy-fault branch, integer overflow, aliased
buffers, development gating, unsupported operations and registration precedence.
The existing metadata lifecycle test passes. The DEVELOPMENT ARM64 B2L kernel
build includes the new callback and the already tested 0059 framebuffer fixes.
Firmware, touch support, memory reservations and CPU topology remain from 0058.

A complete BOOT prefix readback verifies installation. The expected improvement
is removal of libxpc's internally inconsistent-match-result assertion. The
caller still receives a real unsupported result and may reject the operation.
Successful backboardd startup and first-frame presentation require subsequent
device runtime evidence; this is not an implementation of an AMFI matcher.
Private captures and installation receipts stay local.
