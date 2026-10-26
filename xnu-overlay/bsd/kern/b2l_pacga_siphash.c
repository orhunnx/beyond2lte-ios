/* SPDX-License-Identifier: BSD-2-Clause
 * Narrow Mach/BSD bridge using pinned XNU's existing SipHash implementation.
 * This is the opt-in B2L development PACGA result ABI, not QARMA emulation.
 */
#include <stdint.h>
#include <net/siphash.h>

uint64_t
b2l_pacga_siphash(uint64_t a, uint64_t b, const uint64_t key[2])
{
    SIPHASH_CTX ctx;
    /* B2L is little endian: LE64(a) || LE64(b). SipHash_End wipes the
     * temporary keyed context. Preserve PACGA's zero low32 result shape. */
    const uint64_t message[2] = {a, b};
    return SipHashX(&ctx, 2, 4, (const uint8_t *)key,
        message, sizeof(message)) & UINT64_C(0xffffffff00000000);
}
