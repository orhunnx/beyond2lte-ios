/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef B2L_BOOT_DEVICE_PATH_H
#define B2L_BOOT_DEVICE_PATH_H
#include "BootArgs.h"
/* Match the actual Exynos UfsDxe HW_VENDOR_DP, not a guessed standard UFS
 * node. Its GUID is public driver identity, not a device-specific identifier. */
static inline int b2l_boot_ufs_lun(const void *data,b2l_size size,b2l_u32 *lun)
{
    static const unsigned char guid[16]={0x91,0x8e,0x2c,0x5f,0x3d,0x7a,0x6e,0x4b,
        0x9c,0x14,0xf0,0x2a,0x68,0xb7,0x3d,0xc1};
    const unsigned char *p=data;
    b2l_size off=0;
    unsigned nodes=0,found=0,partition=0;
    b2l_u32 result=0;
    if(!p || !lun || size<4 || size>4096)return -1;
    while(off<size && ++nodes<=128) {
        unsigned length,match=1;
        if(size-off<4)return -1;
        length=(unsigned)p[off+2]|(unsigned)p[off+3]<<8;
        if(length<4 || length>size-off)return -1;
        if(p[off]==0x7f) {
            if(p[off+1]!=0xff || length!=4 || off+4!=size || found!=1 || !partition)return -1;
            *lun=result;return 0;
        }
        if(p[off]==1 && p[off+1]==4 && length>=20) {
            for(unsigned i=0;i<16;i++)if(p[off+4+i]!=guid[i])match=0;
            if(match) {
                if(length!=21 || found || partition || p[off+20]>7)return -1;
                found=1;result=p[off+20];
            }
        }
        if(p[off]==4 && p[off+1]==1) {
            if(length!=42 || !found || partition || p[off+40]!=2 || p[off+41]!=2)return -1;
            partition=1;
        }
        off+=length;
    }
    return -1;
}
#endif
