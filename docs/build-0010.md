# Build 0010: AES mode adapter for TCP initialization

## Confirmed failure

Build 0009 passes pthread initialization and reaches DLIL networking. The exact
built kernel's DWARF identifies the next data abort as aes_encrypt_key in
corecrypto_aes.c:38, called during tcp_init at tcp_subr.c:660. The global
external crypto function table is NULL.

A5/SWEET patch 0020 and A5 patch 0098 provide the applicable architecture:
real portable AES CBC/ECB behind XNU's existing mode API. A B2L-specific adapter
must supply actual block encryption and decryption, keep unsupported modes
unavailable and avoid registering an incomplete global crypto function table.

## Storage boundary

The user separately authorized replacing SYSTEM with an empty HFS image if
needed, after a complete backup of the existing contents. No SYSTEM write is
required for this AES startup fix. The recovery inventory showed a SYSTEM/super
alias, so any later disk work must establish the logical container layout and
preserve a verified complete backup before writing. Raw inventory, backups and
identifiers stay local and ignored.

## Implementation and host verification

The adapter uses the MIT-licensed BearSSL portable bitsliced AES implementation,
pinned to `7bea48e5e850ab4cafbe68d3765cdaba13a86d6f`. Only the required AES
sources are staged locally. Real CBC/ECB encryption and decryption support
128/192/256-bit keys, including the legacy API's byte and bit length forms.
The mode context fits the existing XNU storage. General-register-only builds
avoid assuming kernel SIMD state management.

The global crypto table is not fabricated. All 18 GCM APIs reject unavailable
or incomplete providers before dispatch, returning error or zero context size
without producing authenticated output. Other external crypto services remain
outside this adapter; later TCP connection paths still require an MD5 provider.

The standalone kernel/dSYM build succeeded. The kernel is 17,985,664 bytes;
the verified BOOT image is 22,559,168 bytes. The full patch applies to the
pristine pinned XNU index and overlays match the built source. The unchanged
firmware's exact embedded loader and BOOT payload layout were verified.

ASan/UBSan tests compile the actual adapter, wrappers and pinned BearSSL sources.
AES128/192/256 CBC and ECB encryption/decryption match NIST SP800-38A vectors.
Checks also cover context copying, in-place and overlapping buffers, canaries,
constant versus chained IV behavior, NULL IV as zero, zero blocks, invalid keys,
length overflow and unavailable/partial GCM providers. Independent source review
confirmed context sizes, API units, IV semantics and real linked AES symbols.
These checks do not establish physical progress past TCP initialization.

## Installation and device boundary

The authorized BOOT-only update completed after a full verified backup of the
previous BOOT. The original backup remains intact. Exact image-prefix readback
matched, then the device was rebooted. SYSTEM was only inspected read-only; no
SYSTEM/HFS image was written. Raw receipts and identifiers stay local and ignored.

The next screen report must
establish progress past TCP initialization, followed by the next startup stage.
No root filesystem or native UFS driver is supplied by this AES-only change.

## Device result

The screen report confirms progress past TCP/AES initialization. Skywalk's
copy, zeroing and masked-compare self-tests all pass. BSD autoconfiguration
then reaches kminit. The next panic asserts _unsafe_kernel_text_initialized
in machine_routines.c:3379, because the EFI device tree omits the chosen
kernel-ctrr-to-be-enabled property required by the DEVELOPMENT kernel.
No raw photograph or dump is committed. This failure is a boot metadata
contract gap; it is not evidence of an AES test failure.
