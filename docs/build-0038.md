# Build 0038: missing persona manifest bootstrap

## Recovered 0037 evidence

Epoch 10 contains 213 valid records and 290456 text bytes, starting at sequence
zero, with no missing positions or producer drops. The final saved line is
incomplete. FIPS explicitly passes and no early panic is present in the capture.
IOWatchdog registers and selector 0 succeeds. Neither the earlier watchdog
service-discovery failure nor the earlier SpringBoard abort is observed.
This absence does not establish a usable UI or prove every initializer ran.

Repeated usermanagerd exits report failure to load its persona manifest.
A fresh read-only HFS catalog scan finds no keybags directory or persona.kb.
The root remains dirty and is not mounted/replayed/repaired in recovery, so
negative catalog results alone are not conclusive. The daemon's own rotating
log provides independent ENOENT evidence for reading its manifest. Only the
inline-extent prefix of that fragmented log is recovered; older boots occur
in the same file and are not treated as exclusively current-run evidence.

Local daemon disassembly identifies the manifest path as
`/private/var/keybags/persona.kb`. Despite the suffix this particular file is a
plain property-list metadata manifest, not a cryptographic keybag. The embedded
v1 parser accepts numeric UsePersonaManifestVersion and UsePersonaGenerationID,
and a UserPersonaDictionary dictionary; native code has a first-creation path
for an empty user collection. XML plist parsing, the exact schema and initial
values are checked locally. Later daemon behavior remains a runtime test.

## Change

Board patch 0026 runs the new persona helper before the first userspace program,
after HFS mounts and replays its journal and after native-cache application.
It is gated by the existing unsigned development provider and a writable HFS
root. An existing target is preserved, including a non-regular target; a lookup
error other than ENOENT stops provisioning.

Only the keybags directory and an initial empty v1 metadata manifest are seeded.
A private staging inode is root-owned, mode 0600, regular, on the root mount,
and has one link. Existing staging contents must exactly match a prefix of the
fixed seed and cannot exceed its length. Interrupted matching prefixes can be
completed. The complete contents are synced and read back before an exclusive
hard link publishes the final name. An existing final name cannot be replaced.
The parent directory and filesystem are then synced. The staging name is retained
as the initial seed; no original manifest is removed or rewritten.

Power loss before publication leaves no partial final manifest; a completed
staging inode can be reused. A corrupt, oversized or unexpected staging object
is rejected rather than overwritten. This is metadata initialization, not SEP,
key generation or encrypted persona-volume support. Existing securityd,
ManagedSettings and mobileactivation failures are not claimed fixed.

## Verification and installation

ASan/UBSan tests exercise the actual seed writer and boot sequencer with missing
and existing targets, interrupted prefixes, matching completed staging, invalid
staging, permissions and I/O failures. The test stubs VFS namei/publication;
real HFS publication still needs device confirmation. The real 25-to-26 patch
sequence passes with idempotence and local-edit refusal. The DEVELOPMENT kernel
build passes, its new object is newer than the final source edit, and scalar
machine-code inspection finds no FP/SIMD operands. Matching kernel/unstripped/
dSYM UUIDs are retained privately; the packaged embedded loader is verified.

BOOT size: 23186152 bytes. SHA-256:
`20662f9005abb10871334ff13580874f62b36c9faab985f1558976b833ef4292`.
Installed 0037 BOOT was verified before writing; full 0038 image-prefix readback
matches after writing. Reboot was requested. Recovery performed no raw root
writes. On the next XNU boot the helper is authorized to create only missing
bootstrap metadata through mounted HFS. Expected marker:
`B2L PERSONA: initial v1 manifest published after journal replay`.

No daemon launch success or graphical startup is claimed before the next log.
Private logs, catalogs, firmware and binaries remain ignored local artifacts.
