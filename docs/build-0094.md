# Build0094: trace the remaining CoreGraphics NULL contexts

## Fresh0093 result

The owner reports both malformed icons and a black screen after tapping Settings
still occur.0093 did not resolve those visible problems.

Recovery collection saved kernel epoch54 and the complete native/supplemental
archive without device writes or HFS journal replay. The export reports all
selected files complete and the volume header unchanged. Kernel framing has
20 missing positions and23 producer drops. Current-boot decoding yields85,553
events with UUID10A5BF5599AD4CB6AE7EFC094EDE32EE and8 parser-warning lines.
Raw evidence remains private under ignored out/logs/0093-recovery.

The kernel confirms the corrected16384x16384 IOSurface limits. No `Failed to
render` or `too big` message was found in the current native snapshot, compared
with8 paired CoreImage failures in0092. This supports resolution of that specific
limit defect; it does not establish complete image rendering or UI stability.

Presentation diagnostics reach152 swaps and150 copied layers, with zero missing
surfaces, unsupported formats/mappings or bounds failures. Two swap records do
not result in a copy and4 records update disabled layers; those are different
units. The display goes through blanked/off then LockButton/on, and copying
resumes. These are retained events, not proof that the owner's final black
screen recovered. Native events stop before the final retained touch sequence;
there is still no captured Preferences launch establishing its failure.

Remaining SpringBoard errors include `verify_image_parameters: invalid image
colorspace: NULL` and164 invalid-context calls. The context errors occur in
various drawing/text/image operations; callers must be identified before
choosing a replacement colorspace or changing rendering behavior. A separate
read-only HFS inspection verifies14 ICC profile files match the prepared root
image, including sRGB and Display P3. Neither missing profiles nor a failed
physical framebuffer copy is established as the cause. Chronod HEVC/YUV
allocation failures remain separate, without a demonstrated Settings link.

## Scoped diagnostic change

The shipped CoreGraphics `_handle_invalid_context` checks the presence of
`CG_CONTEXT_SHOW_BACKTRACE`. When present it calls CGBacktraceCreate and logs
the result through CGPostError; this is verified in the local pristine binary.

Under the existing explicit `b2l_diag=1` boot mode, the exec string-copy stage
now adds `CG_CONTEXT_SHOW_BACKTRACE=1` only for these exact executable paths:

- /System/Library/CoreServices/SpringBoard.app/SpringBoard
- /usr/libexec/backboardd
- /Applications/Preferences.app/Preferences

It leaves an existing value unchanged and scans only environment strings already
copied into kernel-owned exec storage. The regular exec string copier accounts
for bytes; the helper accounts for the new pointer and environment count. If
there is insufficient room, it leaves the original exec environment unchanged.
The existing NULL terminator and alignment stages still run. The addition is
absent when the boot argument is missing,0 or another value. No trust flag,
entitlement, task privilege, iOS binary, plist or root-volume file is changed.
The normal CoreGraphics error behavior remains; this is a diagnostic trial,
not a claim that the NULL colorspace or Settings black screen is fixed.

## Checks

ASan/UBSan executes the actual helper and pinned XNU exec_add_user_string:
exact-path matching, opt-in/off modes, preserving unrelated strings, preserving
an existing diagnostic value, no duplication,32/64-bit pointer accounting,
insufficient space and copy-error handling. Production XNU compiles/links.
The patch's reverse application check passes on the prepared source. Scalar
cache/direct-VM API audits pass and the linked kernel contains zero RCpc
instructions.0093 firmware/loader and rendering behavior are retained.

## Next runtime evidence

After the UI appears, reproduce the malformed icons and Settings black screen.
Leave the system running for at least60seconds after the failure before moving
to recovery, allowing native log buffers to persist. The next archive must show
the B2L CG DIAG marker and CoreGraphics backtraces; absence is not resolution.

## Installed

Only BOOT was written after recovery/product/root/partition/capacity checks, a
full57,671,680-byte BOOT backup and exact0093-prefix verification. The
23,536,080-byte0094 image and exact readback share SHA256:

`435423a684f5c4b65e47523615b13b4c15c2b254b6d35d76a68923077b8e1500`

The reboot command completed. Image, symbols, backup and receipt remain in
ignored private out/0094. Native backtrace emission and the remaining graphics
cause require the next run; this installation does not establish a UI fix.
