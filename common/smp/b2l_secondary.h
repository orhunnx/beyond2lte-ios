/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef B2L_SECONDARY_H
#define B2L_SECONDARY_H
#define B2L_SECONDARY_BYTES 16384ULL
#define B2L_SECONDARY_TEMPLATE_BYTES 152U
#define B2L_SECONDARY_MAGIC 0x3130504d534c3242ULL
#define B2L_SECONDARY_TARGET_WORD 5
#define B2L_SECONDARY_STAGE_WORD 6
#define B2L_SECONDARY_IDENTITY_WORD 16
/* This is a firmware-owned allocation, never general-purpose XNU RAM. */
static inline int b2l_secondary_address(unsigned long long pa)
{
    if ((pa & (B2L_SECONDARY_BYTES-1)) || pa < 0x80000000ULL ||
        pa > 0x100000000ULL-B2L_SECONDARY_BYTES) return 0;
    const unsigned long long forbidden[][2] = {
        {0x87000000ULL,0x87200000ULL}, /* Samsung U-H */
        {0xc9000000ULL,0xc9440000ULL}, /* FD and stack */
        {0xca000000ULL,0xcb400000ULL}, /* framebuffer */
        {0xcf000000ULL,0xcf050000ULL}  /* auxiliary EL2 and RT log */
    };
    for (unsigned i=0;i<sizeof(forbidden)/sizeof(forbidden[0]);++i)
        if (pa<forbidden[i][1] && forbidden[i][0]<pa+B2L_SECONDARY_BYTES) return 0;
    return 1;
}
static inline int b2l_secondary_template(const volatile unsigned long long *p)
{
    return p && p[0]==0x10000171d5034fdfULL && p[1]==0xf9000230d2800030ULL &&
        p[2]==0xd5033f9fd50b7e31ULL && p[3]==0x1400000958000090ULL &&
        p[4]==B2L_SECONDARY_MAGIC && p[7]==2 &&
        p[8]==0xd538424f10000211ULL &&
        p[9]==0xd53800aff900022fULL &&
        p[10]==0xd538000ff900062fULL &&
        p[11]==0xd50b7e31f9000a2fULL &&
        p[12]==0x10fffe71d5033f9fULL &&
        p[13]==0xf900022fd280004fULL &&
        p[14]==0xd5033f9fd50b7e31ULL &&
        p[15]==0xd503201fd61f0200ULL;
}
#endif
