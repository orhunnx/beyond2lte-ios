# Proposed unavailable response for AMFI operation 0x66

Status: explicitly approved for implementation in build 0060 after review of
this exact scope. Builds 0058 and 0059 did not include the AMFI MAC policy.
See [build-0060.md](build-0060.md) for the implemented response and verification.

## Exact proposed scope

- Register a permanent MAC policy named AMFI only in the already explicit B2L
  unsigned development mode. A preexisting policy of that name wins.
- Implement only operation 0x66's unavailable-response contract. It never
  evaluates a constraint as matched and never produces a successful identity
  or signature validation result.
- Validate the native 64-byte request, bounded constraint length, output pointer
  and 1028-byte result capacity. Preserve EFAULT for user-copy failures and
  EINVAL for malformed sizes. Do not dereference unvalidated user pointers.
- For a structurally valid request, copy out an initialized 1028-byte result:
  error_code=3 and an explicit description that launch-constraint matching is
  unavailable in B2L development mode. Then return ENOTSUP. The native result
  code 3 is used for unsupported constraint types; this response does not claim
  that a supplied requirement was evaluated against the process.
- Copy out the failure result even when returning an error. This preserves
  consistency between the caller's false match result and its error field.
- Other AMFI operations return ENOTSUP. This is a visible change from the
  current absent-policy ENOPOLICY response and must be included in approval.
- No changes to code-signing flags, entitlement contents, CodeDirectory/CDHash,
  process ownership, trust caches, Mach port provenance or signature checking.

## Verification and expected limit

Before installation, execute the actual callback with valid requests, bad
sizes, allocation/user-copy failures, disabled development mode and preexisting
policy registration. Check that every path remains nonmatching, failure output
is initialized and all unknown operations stay unsupported.

The expected improvement is removal of libxpc's inconsistent-match-result
assertion. The caller still receives a real unsupported/failure result and can
reject the operation. This does not establish that backboardd will complete
startup or that the UI will appear.
