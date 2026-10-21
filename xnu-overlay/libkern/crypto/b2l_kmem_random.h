/* SPDX-License-Identifier: BSD-2-Clause
 * Allocator-only SplitMix64. This meets crypto/rand.h's explicitly
 * non-cryptographic, lock-free kmem context contract. It is not /dev/random.
 * SplitMix64 transition by Sebastiano Vigna, public-domain reference.
 */
#ifndef B2L_KMEM_RANDOM_H
#define B2L_KMEM_RANDOM_H
#include <stdint.h>
#include <stddef.h>
struct b2l_kmem_rng { uint64_t state; };
static inline uint64_t b2l_kmem_next(struct b2l_kmem_rng *ctx)
{
	uint64_t value = (ctx->state += UINT64_C(0x9e3779b97f4a7c15));
	value = (value ^ (value >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
	value = (value ^ (value >> 27)) * UINT64_C(0x94d049bb133111eb);
	return value ^ (value >> 31);
}
static inline uint64_t b2l_kmem_bounded(struct b2l_kmem_rng *ctx, uint64_t bound)
{
	/* Callers require bound>0. Reject the incomplete residue interval. */
	const uint64_t threshold = -bound % bound;
	uint64_t value;
	do { value = b2l_kmem_next(ctx); } while (value < threshold);
	return value % bound;
}
static inline void b2l_kmem_generate(struct b2l_kmem_rng *ctx, void *out, size_t size)
{
	unsigned char *bytes = out;
	while (size) {
		uint64_t value = b2l_kmem_next(ctx);
		for (unsigned n = 0; n < 8 && size; ++n, --size) {
			*bytes++ = (unsigned char)value; value >>= 8;
		}
	}
}
#endif
