#include "b2l_cache_core.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
int main(void){
 unsigned char hash[32];b2l_sha256("abc",3,hash);const unsigned char abc[32]={0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad};assert(!memcmp(hash,abc,32));
 unsigned char multiblock[4096];for(unsigned i=0;i<4096;i++)multiblock[i]=(unsigned char)i;
 const unsigned char expected[32]={0xc8,0xf5,0xd0,0x34,0x1d,0x54,0xd9,0x51,0xa7,0x1b,0x13,0x6e,0x6e,0x2a,0xfc,0xb1,0x4d,0x11,0xed,0x84,0x89,0xa7,0xae,0x12,0x6a,0x8f,0xee,0x0d,0xf6,0xec,0xf1,0x93};struct b2l_sha stream;b2l_sha_init(&stream);
 for(unsigned i=0;i<4096;i+=13)b2l_sha_update(&stream,multiblock+i,4096-i<13?4096-i:13);
 b2l_sha_final(&stream,hash);assert(!memcmp(hash,expected,32));
 unsigned char*c=calloc(1,B2L_NC_CELL),page[4096];assert(c);unsigned char*r=c;const unsigned char*out;uint32_t native;
 assert(b2l_nc_native(0xd65f0fff,&native)&&native==0xd65f03c0);
 assert(b2l_nc_native(0xd71f0a11,&native)&&native==0xd61f0200);
 assert(b2l_nc_native(0xd73f0a11,&native)&&native==0xd63f0200);
 assert(b2l_nc_native(0xf8bfc020,&native)&&native==0xc8dffc20);
 assert(!b2l_nc_native(0x9ac03000,&native));assert(!b2l_nc_native(0xf8200400,&native));assert(!b2l_nc_native(0xdeadbeef,&native));
 memcpy(r,"B2LNCREC",8);b2l_nc_p64(r+8,1);b2l_nc_p64(r+16,42);b2l_nc_p64(r+24,8192);r[40]=7;b2l_nc_p32(r+56,1);b2l_nc_p32(r+60,4096);
 b2l_nc_p32(r+B2L_NC_DATA,0xd65f0fff);b2l_nc_p32(r+B2L_NC_DATA+4096,0xd65f03c0);b2l_nc_seal(r);assert(!b2l_nc_record(r));assert(!b2l_nc_select(c,&out)&&out==r);
 memcpy(page,r+B2L_NC_DATA,4096);assert(b2l_nc_page_state(r,page,4096)==0);memcpy(page,r+B2L_NC_DATA+4096,4096);assert(b2l_nc_page_state(r,page,4096)==1);/* Any prefix of a page write is repaired from the durable record. */
 for(unsigned cut=0;cut<=4096;cut++){memcpy(page,r+B2L_NC_DATA,4096);memcpy(page,r+B2L_NC_DATA+4096,cut);assert(b2l_nc_page_state(r,page,4096)>=0);}
 page[55]=1;assert(b2l_nc_page_state(r,page,4096)==-1);
 memcpy(c+B2L_NC_COPY,c,B2L_NC_COPY);b2l_nc_p64(c+B2L_NC_COPY+8,2);b2l_nc_seal(c+B2L_NC_COPY);assert(!b2l_nc_select(c,&out)&&out==c+B2L_NC_COPY);
 /* Every possible tear between an old durable and new copy must retain old. */
 unsigned char*full=malloc(B2L_NC_COPY);memcpy(full,c+B2L_NC_COPY,B2L_NC_COPY);
 for(unsigned n=0;n<B2L_NC_COPY;n++){memset(c+B2L_NC_COPY,0,B2L_NC_COPY);memcpy(c+B2L_NC_COPY,full,n);assert(!b2l_nc_select(c,&out)&&out);if(out!=c)assert(!memcmp(out,full,B2L_NC_COPY));}
 memcpy(c+B2L_NC_COPY,full,B2L_NC_COPY);b2l_nc_p64(c+B2L_NC_COPY+8,1);c[B2L_NC_COPY+40]^=1;b2l_nc_seal(c+B2L_NC_COPY);assert(b2l_nc_select(c,&out)==-1);
 memset(c+B2L_NC_COPY,0,B2L_NC_COPY);b2l_nc_p32(c+B2L_NC_DATA+4096,0xd4200000);b2l_nc_seal(c);assert(b2l_nc_record(c));
 free(full);free(c);puts("SHA256, native encodings, unsupported rejection, stale page, dual copy and all torn-write offsets passed");
}
