/* SPDX-License-Identifier: BSD-2-Clause
 * Match an owned executable after the existing observed-instruction cache has
 * applied its allowlisted rewrites. Non-code bytes still match exactly.
 */
#ifndef B2L_APPS_PAYLOAD_COMPARE_H
#define B2L_APPS_PAYLOAD_COMPARE_H
#include "b2l_cache_core.h"
static bool b2l_apps_code_word(const unsigned char *image,size_t bytes,size_t word)
{
    if(bytes<32||word>bytes||bytes-word<4||(word&3)||
       b2l_nc_u32(image)!=0xfeedfacf||b2l_nc_u32(image+4)!=0x100000c||
       (b2l_nc_u32(image+8)&0xffffff)!=2)return false;
    uint32_t count=b2l_nc_u32(image+16),length=b2l_nc_u32(image+20);
    if(!count||count>1024||length>bytes-32)return false;
    size_t at=32,end=32+length;bool found=false;
    for(uint32_t i=0;i<count;i++){
        if(at>end||end-at<8)return false;
        uint32_t cmd=b2l_nc_u32(image+at),n=b2l_nc_u32(image+at+4);
        if(n<8||(n&7)||n>end-at)return false;
        if(cmd==0x19){
            if(n<72)return false;
            uint32_t sections=b2l_nc_u32(image+at+64);
            uint64_t file=b2l_nc_u64(image+at+40),size=b2l_nc_u64(image+at+48);
            if(sections>(n-72)/80||file>bytes||size>bytes-file)return false;
            bool executable=(b2l_nc_u32(image+at+60)&4)!=0;
            for(uint32_t j=0;j<sections;j++){
                const unsigned char *s=image+at+72+(size_t)j*80;
                uint64_t offset=b2l_nc_u32(s+48),span=b2l_nc_u64(s+40);
                if(!executable||!(b2l_nc_u32(s+64)&0x80000000u))continue;
                if((offset&3)||(span&3)||offset<file||offset-file>size||span>size-(offset-file))return false;
                if(word>=offset&&word-offset<span&&span-(word-offset)>=4){
                    if(found)return false;
                    found=true;
                }
            }
        }
        at+=n;
    }
    return at==end&&found;
}
static bool b2l_apps_payload_equal(const unsigned char *source,size_t bytes,
    size_t offset,const unsigned char *actual,size_t length,bool allow_native)
{
    if(offset>bytes||length>bytes-offset)return false;
    if(!memcmp(source+offset,actual,length))return true;
    if(!allow_native||(offset&3)||(length&3))return false;
    for(size_t i=0;i<length;i+=4){
        uint32_t original=b2l_nc_u32(source+offset+i),current=b2l_nc_u32(actual+i),native;
        if(original==current)continue;
        if(!b2l_apps_code_word(source,bytes,offset+i)||!b2l_nc_native(original,&native)||current!=native)return false;
    }
    return true;
}
#endif
