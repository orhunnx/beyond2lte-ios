#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "b2l_amfi_metadata.h"
static uint8_t disk[4096];static uint64_t fail_at=UINT64_MAX;
static int rd(void*o,uint64_t at,void*out,size_t n){(void)o;if(at==fail_at||at>sizeof(disk)||n>sizeof(disk)-at)return -1;memcpy(out,disk+at,n);return 0;}
static void le(uint8_t*p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(8*i));}
static void be(uint8_t*p,uint32_t v){for(unsigned i=0;i<4;i++)p[3-i]=(uint8_t)(v>>(8*i));}
static unsigned minimum(unsigned v){if(v>=0x20600)return 108;if(v>=0x20500)return 96;if(v>=0x20400)return 88;if(v>=0x20300)return 64;if(v>=0x20200)return 52;if(v>=0x20100)return 48;return 44;}
static uint8_t*fixture(unsigned version,const char*id,size_t length){memset(disk,0,sizeof(disk));unsigned m=minimum(version),cd=m+(unsigned)length,total=20+cd;le(disk,0xfeedfacf);le(disk+4,0x100000c);le(disk+12,2);le(disk+16,1);le(disk+20,16);le(disk+32,0x1d);le(disk+36,16);le(disk+40,256);le(disk+44,total);uint8_t*s=disk+256;be(s,0xfade0cc0);be(s+4,total);be(s+8,1);be(s+12,0);be(s+16,20);uint8_t*c=s+20;be(c,0xfade0c02);be(c+4,cd);be(c+8,version);be(c+20,m);memcpy(c+m,id,length);return c;}
static int run(size_t capacity,const char*expect){struct b2l_amfi_reader r={sizeof(disk),rd,NULL};uint8_t out[1024];memset(out,0xaa,sizeof(out));size_t n=999;int result=b2l_amfi_read_identity(&r,0,out,capacity,&n);if(result==0){assert(expect&&n==strlen(expect)+1&&!memcmp(out,expect,n));}else{assert(n==0&&(!capacity||!out[0]));}return result;}
int main(void){const char id[]="example.fixture";
 for(unsigned v=0x20000;v<=0x20600;v+=0x100){fixture(v,id,sizeof(id));assert(run(1024,id)==0);}
 uint8_t*c=fixture(0x20600,id,sizeof(id));be(c,0xfade7171);assert(run(1024,NULL)==B2L_AMFI_XML_INVALID);
 c=fixture(0x20600,id,sizeof(id));be(c+20,107);assert(run(1024,NULL)==B2L_AMFI_XML_INVALID);
 c=fixture(0x20500,id,sizeof(id));be(c+4,88);assert(run(1024,NULL)==B2L_AMFI_XML_INVALID);
 c=fixture(0x20600,id,sizeof(id));be(c+8,0x20700);assert(run(1024,NULL)==B2L_AMFI_XML_INVALID);
 c=fixture(0x20000,id,sizeof(id));be(c+8,0x10000);assert(run(1024,NULL)==B2L_AMFI_XML_INVALID);
 c=fixture(0x20000,id,sizeof(id));be(c+20,44+sizeof(id));assert(run(1024,NULL)==B2L_AMFI_XML_INVALID);
 fixture(0x20600,"",1);assert(run(1024,NULL)==B2L_AMFI_XML_INVALID);
 c=fixture(0x20600,id,sizeof(id));c[108+sizeof(id)-1]='x';assert(run(1024,NULL)==B2L_AMFI_XML_INVALID);
 fixture(0x20600,id,sizeof(id));assert(run(sizeof(id)-1,NULL)==B2L_AMFI_XML_INVALID);
 fixture(0x20600,id,sizeof(id));fail_at=256+20+108;assert(run(1024,NULL)==B2L_AMFI_XML_IO);fail_at=UINT64_MAX;
 fixture(0x20600,id,sizeof(id));be(disk+256+12,0x1000);assert(run(1024,NULL)==B2L_AMFI_XML_ABSENT);
 char longest[1024];memset(longest,'a',sizeof(longest));longest[1023]=0;fixture(0x20600,longest,sizeof(longest));assert(run(1024,longest)==0);
 char too_long[1025];memset(too_long,'a',sizeof(too_long));too_long[1024]=0;fixture(0x20600,too_long,sizeof(too_long));assert(run(1024,NULL)==B2L_AMFI_XML_INVALID);
 fixture(0x20600,id,sizeof(id));struct b2l_amfi_reader r={sizeof(disk),rd,NULL};size_t n=9;assert(b2l_amfi_read_identity(&r,0,NULL,0,&n)==B2L_AMFI_XML_INVALID&&n==0);
 puts("Actual CodeDirectory identity reader: versions/bounds/NUL/primary-slot/1024limit/errors PASS");return 0;}
