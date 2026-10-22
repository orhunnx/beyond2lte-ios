/* SPDX-License-Identifier: BSD-2-Clause */
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "b2l_amfi_metadata.h"
static uint8_t data[4096];static int fail;
static void le(uint8_t*p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(8*i));}
static void be(uint8_t*p,uint32_t v){for(unsigned i=0;i<4;i++)p[3-i]=(uint8_t)(v>>(8*i));}
static int rd(void*o,uint64_t at,void*out,size_t n){(void)o;if(fail||at>sizeof(data)||n>sizeof(data)-at)return -1;memcpy(out,data+at,n);return 0;}
static void valid(void){memset(data,0,sizeof(data));le(data,0xfeedfacf);le(data+4,0x100000c);le(data+12,2);le(data+16,1);le(data+20,16);le(data+32,0x1d);le(data+36,16);le(data+40,256);le(data+44,128);be(data+256,0xfade0cc0);be(data+260,30);be(data+264,1);be(data+268,7);be(data+272,20);be(data+276,0xfade7172);be(data+280,10);data[284]=0x30;}
static int run(size_t cap){struct b2l_amfi_reader r={sizeof(data),rd,NULL};uint8_t out[65536];size_t n=999;int e=b2l_amfi_read_der(&r,0,out,cap,&n);assert(e==0?(n==10&&!memcmp(out,data+276,10)):n==0);return e;}
int main(void){
 valid();assert(run(65536)==0);assert(run(9)==B2L_AMFI_XML_INVALID);
 fail=1;assert(run(65536)==B2L_AMFI_XML_IO);fail=0;
 valid();be(data+268,6);assert(run(65536)==B2L_AMFI_XML_ABSENT);
 valid();be(data+276,0xfade7171);assert(run(65536)==B2L_AMFI_XML_INVALID);
 valid();be(data+280,65537);assert(run(65536)==B2L_AMFI_XML_INVALID);
 valid();be(data+272,19);assert(run(65536)==B2L_AMFI_XML_INVALID);
 valid();be(data+260,50);be(data+264,2);be(data+272,28);be(data+276,7);be(data+280,40);
 be(data+284,0xfade7172);be(data+288,10);be(data+296,0xfade7172);be(data+300,10);assert(run(65536)==B2L_AMFI_XML_INVALID);
 uint32_t rng=3;struct b2l_amfi_reader r={sizeof(data),rd,NULL};uint8_t out[65536];
 for(unsigned i=0;i<20000;i++){valid();rng=rng*1664525u+1013904223u;unsigned at=rng%384;rng=rng*1664525u+1013904223u;data[at]=(uint8_t)rng;size_t n=999;int e=b2l_amfi_read_der(&r,0,out,sizeof(out),&n);assert(e==0?(n>8&&n<=sizeof(out)):n==0);}
 puts("DER metadata: bounded opaque copies, absence, malformed/duplicate/overlap, IO and 20k mutations PASS");
}
