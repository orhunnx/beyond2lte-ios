/* SPDX-License-Identifier: BSD-2-Clause */
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "../uefi/XnuLoader/BootMetadata.h"
int main(void){
 unsigned char field[32]={0},mac[6];memset(mac,255,6);
 assert(b2l_boot_wifi_decode(field,mac)==0 && !memcmp(mac,(unsigned char[6]){0},6));
 memcpy(field,"B2LWIFI1",8);field[8]=2;field[13]=1;
 assert(b2l_boot_wifi_decode(field,mac)==1 && mac[0]==2 && mac[5]==1);
 for(unsigned i=14;i<32;i++){field[i]=1;assert(b2l_boot_wifi_decode(field,mac)==-1);assert(!memcmp(mac,(unsigned char[6]){0},6));field[i]=0;}
 field[0]^=1;assert(b2l_boot_wifi_decode(field,mac)==-1);field[0]^=1;
 field[8]=1;assert(b2l_boot_wifi_decode(field,mac)==-1);
 field[8]=0;field[13]=0;assert(b2l_boot_wifi_decode(field,mac)==-1);
 puts("Optional BOOT WiFi metadata: legacy, valid, corrupt and invalid address checks passed");
 return 0;
}
