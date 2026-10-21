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

The next device test must confirm progress beyond the allocator initialization
fault, followed by boot CPU registration and the dedicated IRQ proof line.
Physical execution of this build is pending.

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
