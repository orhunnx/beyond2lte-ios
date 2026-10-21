/* SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 beyond2lte-ios contributors
 *
 * Original portable cell-array implementation of the QARMA-64 mathematical
 * construction, using five rounds and sigma2 as selected by Arm PACQARMA5.
 * Mathematical parameters: Roberto Avanzi, The QARMA Block Cipher Family,
 * IACR ePrint 2016/444, sections 2-3 and Appendix A.1.
 * QEMU target/arm/tcg/pauth_helper.c (LGPL-2.1-or-later) was consulted as an
 * independent packed-word behavior oracle; its source is not included here.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#include "b2l_pacga_qarma5.h"

typedef struct { uint8_t cell[16]; } block;
/* Cells are numbered from the most significant nibble to the least. */
static const uint8_t sigma[16] = {
    11, 6, 8, 15, 12, 0, 9, 14, 3, 7, 4, 5, 13, 2, 1, 10
};
static const uint8_t tau[16] = {
    0, 11, 6, 13, 10, 1, 12, 7, 5, 14, 3, 8, 15, 4, 9, 2
};
static const uint8_t h[16] = {
    6, 5, 14, 15, 0, 1, 2, 3, 7, 12, 13, 4, 8, 9, 10, 11
};
static const uint64_t constant[5] = {
    UINT64_C(0), UINT64_C(0x13198a2e03707344),
    UINT64_C(0xa4093822299f31d0), UINT64_C(0x082efa98ec4e6c89),
    UINT64_C(0x452821e638d01377)
};

static block unpack(uint64_t word)
{
    block b;
    for (unsigned i = 0; i < 16; ++i)
        b.cell[i] = (uint8_t)((word >> (60 - 4 * i)) & 15);
    return b;
}

static uint64_t pack(block b)
{
    uint64_t word = 0;
    for (unsigned i = 0; i < 16; ++i)
        word = (word << 4) | b.cell[i];
    return word;
}

static block xor_block(block a, block b)
{
    for (unsigned i = 0; i < 16; ++i)
        a.cell[i] ^= b.cell[i];
    return a;
}

static block substitute(block a, int inverse)
{
    for (unsigned i = 0; i < 16; ++i) {
        uint8_t input = a.cell[i];
        uint8_t output = 0;
        /* Fixed-size scan avoids secret-dependent S-box memory addresses. */
        for (unsigned n = 0; n < 16; ++n) {
            unsigned candidate = inverse ? sigma[n] : n;
            unsigned replacement = inverse ? n : sigma[n];
            uint8_t mask = (uint8_t)(0u - (unsigned)(candidate == input));
            output |= (uint8_t)replacement & mask;
        }
        a.cell[i] = output;
    }
    return a;
}

static block permute(block a, const uint8_t permutation[16], int inverse)
{
    block b;
    for (unsigned i = 0; i < 16; ++i) {
        if (inverse)
            b.cell[permutation[i]] = a.cell[i];
        else
            b.cell[i] = a.cell[permutation[i]];
    }
    return b;
}

static uint8_t rotate_cell(uint8_t cell, unsigned count)
{
    return (uint8_t)(((unsigned)cell << count | cell >> (4 - count)) & 15);
}

static block diffuse(block a)
{
    block b;
    for (unsigned col = 0; col < 4; ++col) {
        /* Circulant matrix: [0, R, R^2, R], R = nibble rotate left. */
        for (unsigned row = 0; row < 4; ++row) {
            uint8_t x = 0;
            for (unsigned k = 1; k < 4; ++k)
                x ^= rotate_cell(a.cell[4 * ((row + k) % 4) + col],
                                 k == 2 ? 2 : 1);
            b.cell[4 * row + col] = x;
        }
    }
    return b;
}

static block tweak_step(block t, int inverse)
{
    static const uint8_t lfsr_cells[7] = {0, 1, 3, 4, 8, 11, 13};
    if (!inverse)
        t = permute(t, h, 0);
    for (unsigned i = 0; i < 7; ++i) {
        uint8_t x = t.cell[lfsr_cells[i]];
        if (inverse)
            x = (uint8_t)(((x << 1) & 15) | ((x ^ (x >> 3)) & 1));
        else
            x = (uint8_t)((x >> 1) | (((x ^ (x >> 1)) & 1) << 3));
        t.cell[lfsr_cells[i]] = x;
    }
    return inverse ? permute(t, h, 1) : t;
}

static uint64_t qarma5_cipher(uint64_t data, uint64_t modifier,
                              uint64_t key_lo, uint64_t key_hi)
{
    block t = unpack(modifier);
    block whitening = unpack(key_hi);
    block core = unpack(key_lo);
    uint64_t derived_word = ((key_hi >> 1) | (key_hi << 63)) ^ (key_hi >> 63);
    block derived = unpack(derived_word);
    block s = xor_block(unpack(data), whitening);
    block alpha = unpack(UINT64_C(0xc0ac29b7c97c50dd));

    for (unsigned round = 0; round < 5; ++round) {
        s = xor_block(xor_block(s, core), xor_block(t, unpack(constant[round])));
        if (round != 0)
            s = diffuse(permute(s, tau, 0));
        s = substitute(s, 0);
        t = tweak_step(t, 0);
    }
    s = xor_block(s, xor_block(derived, t));
    s = substitute(diffuse(permute(s, tau, 0)), 0);
    s = permute(xor_block(diffuse(permute(s, tau, 0)), core), tau, 1);
    s = permute(diffuse(substitute(s, 1)), tau, 1);
    s = xor_block(s, xor_block(whitening, t));
    for (unsigned round = 5; round-- > 0;) {
        s = substitute(s, 1);
        if (round != 0)
            s = permute(diffuse(s), tau, 1);
        t = tweak_step(t, 1);
        s = xor_block(xor_block(s, core), xor_block(t, unpack(constant[round])));
        s = xor_block(s, alpha);
    }
    return pack(xor_block(s, derived));
}

uint64_t b2l_pacga_qarma5(uint64_t data, uint64_t modifier,
                         uint64_t key_lo, uint64_t key_hi)
{
    return qarma5_cipher(data, modifier, key_lo, key_hi) &
           UINT64_C(0xffffffff00000000);
}
