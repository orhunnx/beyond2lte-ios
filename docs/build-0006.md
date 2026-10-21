# Build 0006: allocator RNG and kernel PRNG startup

## Confirmed failure

Build 0005 completes VM bootstrap and reaches later startup. Its photographed
data abort was matched to the exact built kernel's symbols and DWARF:
crypto_random_kmem_ctx_size, called by vm_map_store_crypto_init. The global
external crypto function table was NULL. No raw device dump is committed.

## Implementation

The B2L allocator adapter implements the public kmem RNG contract with one
SplitMix64 state per context, seeded from XNU early_random. Generation is
lock-free and bounded values use rejection sampling to avoid modulo bias.
This generator is exclusively for the allocator; the public contract does not
guarantee cryptographic output for those contexts.

A separate real HMAC-SHA256 DRBG registers as the kernel PRNG at EARLY_BOOT
rank FIRST, before the allocator's rank MIDDLE initialization. It uses XNU's
existing DRBG/digest implementations, boot-derived seed material, reseeding,
entropy health checks and serialized state access. SHA-256/SHA-512 wrappers
use the existing built-in digests when the external provider is absent.
cc_rand_generate supplies its full requested length through the real early or
registered DRBG. It does not silently truncate size_t lengths to 32 bits.

These paths do not register an empty or partial global corecrypto table.
Unrelated AES/RSA/SHA-384 provider services remain outside this first-boot
implementation. No zero-data random stub or entropy-check bypass is used.
The physical guard map, 1–2 GiB arena and single-CPU configuration are unchanged.

## Device boundary

The device test passed the allocator initialization fault and PRNG registration.
Boot CPU registration and the dedicated IRQ proof line remain unverified; the
next blocker is described under Device result.

## Build and host verification

The standalone kernel/dSYM target completed successfully. The kernel is
17,946,984 bytes and the validated BOOT image is 22,520,488 bytes, within the
confirmed 57,671,680-byte partition. Existing 0005 EFI firmware is reused;
the packager verified its exact embedded loader.

Tests compile the actual PRNG bridge, linked public upstream HMAC-DRBG and
digest sources under ASAN/UBSAN. Independent Python HMAC/hash oracles verify
initial output, reseeding, refresh and digest answers. Four-thread generation,
allocator known vectors, rejection ranges, unaligned buffers/canaries and
startup ordering also pass. The full patch applies to the pristine pinned
XNU index, and the prepared overlays match the built source.

## Installation

The authorized BOOT-only update completed after a full backup of the previous
image. The original backup remains intact. Readback of the exact image prefix
matched and the device was rebooted. Raw installation receipts and identifiers
remain local and ignored. The following screen report establishes progress
past the prior data abort.

## Device result

The user photograph confirms the HMAC-SHA256 PRNG registration line and later
swap-thread creation, passing the previous allocator-provider fault. The next
panic reports an all-zero invalid ticket lock. Exact-symbol matching identifies
it as vcputc_lock. The B2L early-console path had skipped the stock lifecycle
that initializes its display-control backend and locks. This is a distinct
console lifecycle issue; it is not evidence of random-output failure.
