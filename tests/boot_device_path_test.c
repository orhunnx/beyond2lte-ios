/* SPDX-License-Identifier: BSD-2-Clause */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../uefi/XnuLoader/BootDevicePath.h"
int main(void)
{
    unsigned char path[67]={1,4,21,0,0x91,0x8e,0x2c,0x5f,0x3d,0x7a,0x6e,0x4b,
        0x9c,0x14,0xf0,0x2a,0x68,0xb7,0x3d,0xc1,3,
        4,1,42,0};
    path[61]=2;path[62]=2;path[63]=0x7f;path[64]=0xff;path[65]=4;
    b2l_u32 lun=99;
    assert(!b2l_boot_ufs_lun(path,sizeof(path),&lun) && lun==3);
    for(size_t length=0;length<sizeof(path);length++)assert(b2l_boot_ufs_lun(path,length,&lun));
    unsigned char bad[sizeof(path)];
    memcpy(bad,path,sizeof(path));bad[20]=8;assert(b2l_boot_ufs_lun(bad,sizeof(bad),&lun));
    memcpy(bad,path,sizeof(path));bad[4]^=1;assert(b2l_boot_ufs_lun(bad,sizeof(bad),&lun));
    memcpy(bad,path,sizeof(path));bad[23]=0;assert(b2l_boot_ufs_lun(bad,sizeof(bad),&lun));
    memcpy(bad,path,sizeof(path));bad[62]=0;assert(b2l_boot_ufs_lun(bad,sizeof(bad),&lun));
    memcpy(bad,path,sizeof(path));bad[64]=1;assert(b2l_boot_ufs_lun(bad,sizeof(bad),&lun));
    memcpy(bad,path,sizeof(path));bad[2]=0xff;bad[3]=0xff;assert(b2l_boot_ufs_lun(bad,sizeof(bad),&lun));
    assert(b2l_boot_ufs_lun(path,25,&lun));
    puts("EFI BOOT vendor LUN path and malformed-node bounds tests passed");
}
