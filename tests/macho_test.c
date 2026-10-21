#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../uefi/XnuLoader/MachO.h"
static unsigned char image[8192];
static void w32(size_t p, uint32_t n) { for (unsigned i=0;i<4;i++) image[p+i]=(unsigned char)(n>>(8*i)); }
static void w64(size_t p, uint64_t n) { for (unsigned i=0;i<8;i++) image[p+i]=(unsigned char)(n>>(8*i)); }
static void fixture(void) {
    memset(image,0,sizeof image);
    w32(0,0xfeedfacf);w32(4,0x0100000c);w32(12,2);w32(16,2);w32(20,360);
    w32(32,0x19);w32(36,72);w64(56,0xfffffff007004000ULL);w64(64,8192);
    w64(80,8192);w32(92,5);
    w32(104,5);w32(108,288);w32(112,6);w32(116,68);w64(376,0xfffffff007005000ULL);
}
int main(void) {
    struct b2l_macho out;
    fixture(); assert(b2l_parse_macho(image,sizeof image,&out)==0);
    assert(out.entry==0xfffffff007005000ULL && out.segment_count==1);
    for (size_t n=0;n<392;n++) assert(b2l_parse_macho(image,n,&out)!=0);
    fixture();w32(100,1);assert(b2l_parse_macho(image,sizeof image,&out)!=0);
    fixture();w32(8,2);assert(b2l_parse_macho(image,sizeof image,&out)!=0);
    fixture();w32(36,0xfffffff8);assert(b2l_parse_macho(image,sizeof image,&out)!=0);
    fixture();w64(80,8193);assert(b2l_parse_macho(image,sizeof image,&out)!=0);
    fixture();w64(376,0xfffffff009000000ULL);assert(b2l_parse_macho(image,sizeof image,&out)!=0);
    fixture();w64(56,0xfffffffffffff000ULL);assert(b2l_parse_macho(image,sizeof image,&out)!=0);
    fixture();w32(16,3);w32(20,376);w32(392,0x80000034);w32(396,16);w32(404,8);
    assert(b2l_parse_macho(image,sizeof image,&out)!=0);
    puts("Mach-O boundary and entry tests passed");return 0;
}
