/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef B2L_BOOT_METADATA_H
#define B2L_BOOT_METADATA_H
/* Optional BOOT-v2 reserved field. Device-specific bytes belong in local
 * packaging input, never in this source or a printed manifest. */
#define B2L_WIFI_MAC_BYTES 6U
#define B2L_BOOT_METADATA_BYTES 32U
static inline int b2l_wifi_mac_valid(const unsigned char mac[6])
{
    unsigned any=0;
    for(unsigned i=0;i<6;i++)any|=mac[i];
    return any && !(mac[0]&1U);
}
/* 0: legacy absent field; 1: valid WiFi metadata; -1: malformed field.
 * All six output bytes are cleared even on absence or failure. */
static inline int b2l_boot_wifi_decode(const unsigned char field[32],unsigned char mac[6])
{
    static const unsigned char tag[8]={'B','2','L','W','I','F','I','1'};
    unsigned any=0;
    for(unsigned i=0;i<6;i++)mac[i]=0;
    for(unsigned i=0;i<32;i++)any|=field[i];
    if(!any)return 0;
    for(unsigned i=0;i<8;i++)if(field[i]!=tag[i])return -1;
    for(unsigned i=14;i<32;i++)if(field[i])return -1;
    if(!b2l_wifi_mac_valid(field+8))return -1;
    for(unsigned i=0;i<6;i++)mac[i]=field[8+i];
    return 1;
}
#endif
