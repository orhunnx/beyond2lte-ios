# Native installation on HFS versus an APFS port

## Current evidence

The shipped23G90 MobileInstallationHelperService rejects /private/var/ on HFS
with MIInstallerErrorDomain166 in MIStagingManager. Its inspected method tests
for apfs explicitly before deriving a volume-UUID staging location. No legacy
switch surrounds that test. The0099 migration found50 candidates, returned
false and installed0 applications. It also selected desired-state6/uninstalled
for many entries; filesystem compatibility is not proven to be the only issue.
0100 did not retry that installation; its retained receipt is from0099.

## HFS route

A real HFS-compatible staging implementation is technically plausible but is
not implemented or validated yet. Merely accepting a filesystem name would not
supply missing operations. Required investigation and validation:

1. Preserve native eligibility, consent, signature and desired-state decisions.
2. Derive a real HFS volume identity and confined staging roots with the same
   ownership, permissions, no-symlink and cross-volume checks.
3. Use actual file copying when clone operations are unsupported, preserving
   contents, modes, ownership, supported xattrs and hard-link relationships.
4. Handle publication, rename/exchange, interruption and cleanup with supported
   atomic HFS operations and durable flushes. Investigate every subsequent
   APFS-specific ioctl and unsupported protection-class request encountered.
5. Test fixture installs and interruption recovery before a separately bounded
   device trial. Retain the current one-attempt receipt until the actual cause
   is addressed and a new trial is explicitly selected.

The current driver already supports journaled root writes and a scoped native
regular-file exchange path. It represents unencrypted FileProtectionNone;
it cannot claim encrypted protection classes. These are useful building blocks,
not proof the entire iOS26 installer works on HFS.

## APFS route and current project cost

Apple documents APFS cloning, snapshots, space sharing and atomic safe-save
features: https://developer.apple.com/documentation/foundation/about-apple-file-system
Apple describes APFS as proprietary:
https://support.apple.com/en-ca/guide/security/seca6147599e/web

The current built kernel has no working APFS filesystem driver. Formatting a
volume would not add one. Native APFS driver integration and writable mount,
flush, interruption and boot validation must precede any migration of device
data. The current HFS volume is not an in-place conversion experiment.

| Area | Impact of an APFS root/data migration in this port |
| --- | --- |
| Native installer | Could satisfy the observed filesystem-type gate; remaining API, desired-state and capability failures still need testing. |
| Boot/root mount | Current USERDATA HFS discovery and HFS driver path must be extended or replaced; boot can regress until tested. |
| Native-code cache | SYSTEM can remain HFS. Root identity, CNID/VFS_VGET lookup, replay/apply guards and undo binding need an APFS design. Existing records cannot simply be rebound to new IDs. Already translated file bytes could be retained by a verified content-preserving migration. |
| Recovery logs/tools | The current read-only raw-volume catalog reader understands HFS, not APFS. Logs remain files but retrieval would require a new reader or compatible mount. |
| Data and rollback | Requires a verified complete backup, a separate staged APFS layout and a restore plan; it is not a BOOT-only update. |
| CPU, UFS, display, touch | Existing hardware support is not inherently lost by choosing APFS; driver validation must still cover the new storage workload. |
| Speed and RAM | No measured comparison exists on this port. APFS neither guarantees faster UI nor inherently fixes NULL colorspaces or missing rendering features. |

Recommendation for the current checkpoint: retain HFS and first map/test the
staging and copy contracts. A full APFS port is a larger independent storage
bring-up. The recommendation is an engineering judgment from current source,
runtime and tool dependencies, not a completed HFS installation solution.
