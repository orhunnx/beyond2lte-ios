# Build0078: copy the complete ABI2 secondary entry in EFI

The supplied0077 screen stops before XNU with `No safe low secondary page:
Unsupported`. AllocateSecondary still required and copied64 bytes after0077
expanded the assembly template to152 bytes. Its length check rejected the
actual template. Changing only the check would also have truncated the code.

Both length validation and CopyMem now use shared
B2L_SECONDARY_TEMPLATE_BYTES=152. Firmware memory ownership, reserved ranges,
16KiB allocation and identity checks remain enforced. The0077 kernel is reused;
this change repairs loader integration, not the still-unverified M4 startup.

The secondary-entry test now runs the actual AllocateSecondary body against
mocked EFI services and the real cross-assembled template. Only physical
memory dereferences and ARM cache-maintenance instructions are substituted
for host execution. Real map selection/ownership validation is retained.
It checks the complete152-byte copy and zeroed remainder, old64-byte ABI
rejection, corruption beyond byte64, allocation retries, failed ownership
validation and identity rejection with release of the allocation. ASan/UBSan
and the existing ELF/COFF/QEMU execution checks pass.

Only BOOT is written after recovery/product/partition/capacity verification,
a full backup and expected0077 prefix match. Exact readback precedes reboot.
M4 success still requires on-device8/8 CPU timer and bidirectional SGI proof.
