/* SPDX-License-Identifier: BSD-2-Clause
 * The paper known answer is independent of this implementation and QEMU.
 * The remaining vectors were generated from QEMU's separately compiled oracle.
 */
#include "b2l_pacga_qarma5.c"
#ifdef B2L_TEST_ASM
extern uint64_t b2l_pacga_qarma5_asm(uint64_t, uint64_t, uint64_t, uint64_t);
#endif
struct vector { uint64_t data, modifier, key_lo, key_hi, full; };
static const struct vector vectors[] = {
#include "test-vectors.inc"
};
int main(void)
{
    for (unsigned i = 0; i < sizeof(vectors) / sizeof(vectors[0]); ++i) {
        const struct vector *v = &vectors[i];
        uint64_t full = qarma5_cipher(v->data,v->modifier,v->key_lo,v->key_hi);
        uint64_t result = b2l_pacga_qarma5(v->data,v->modifier,v->key_lo,v->key_hi);
        if (full != v->full) return 1;
        if (result != (v->full & UINT64_C(0xffffffff00000000))) return 2;
        if ((result & UINT64_C(0xffffffff)) != 0) return 3;
#ifdef B2L_TEST_ASM
        uint64_t fast = b2l_pacga_qarma5_asm(v->data,v->modifier,v->key_lo,v->key_hi);
        if (fast != result) return 4;
#endif
    }
    return 0;
}
