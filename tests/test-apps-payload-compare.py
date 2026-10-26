#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Check actual owned-payload validation against observed native-cache rewrites."""
from pathlib import Path
import argparse,subprocess,tempfile
r=Path(__file__).resolve().parents[1]
pre=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "b2l_apps_payload_compare.h"
static unsigned char source[512],actual[512];
static void fixture(void){
 memset(source,0,sizeof(source));
 b2l_nc_p32(source,0xfeedfacf);b2l_nc_p32(source+4,0x100000c);b2l_nc_p32(source+8,0x80000002);
 b2l_nc_p32(source+16,1);b2l_nc_p32(source+20,152);
 unsigned char *lc=source+32,*s=lc+72;
 b2l_nc_p32(lc,0x19);b2l_nc_p32(lc+4,152);memcpy(lc+8,"__TEXT",6);
 b2l_nc_p64(lc+48,sizeof(source));b2l_nc_p32(lc+60,5);b2l_nc_p32(lc+64,1);
 memcpy(s,"__text",6);memcpy(s+16,"__TEXT",6);b2l_nc_p64(s+40,64);b2l_nc_p32(s+48,192);b2l_nc_p32(s+64,0x80000400);
 b2l_nc_p32(source+192,0xd65f0fff);memcpy(actual,source,sizeof(actual));b2l_nc_p32(actual+192,0xd65f03c0);
}
static unsigned char *load(const char *p,size_t *n){
 FILE*f=fopen(p,"rb");assert(f);assert(!fseek(f,0,SEEK_END));long z=ftell(f);assert(z>0);rewind(f);
 unsigned char*b=malloc((size_t)z);assert(b&&fread(b,1,(size_t)z,f)==(size_t)z);fclose(f);*n=(size_t)z;return b;
}
int main(int argc,char**argv){
 fixture();assert(b2l_apps_payload_equal(source,sizeof(source),0,source,sizeof(source),false));
 assert(!b2l_apps_payload_equal(source,sizeof(source),0,actual,sizeof(actual),false));
 assert(b2l_apps_payload_equal(source,sizeof(source),0,actual,sizeof(actual),true));
 assert(b2l_apps_payload_equal(source,sizeof(source),192,actual+192,4,true));
 actual[193]^=1;assert(!b2l_apps_payload_equal(source,sizeof(source),0,actual,sizeof(actual),true));
 fixture();b2l_nc_p32(source+300,0xd65f0fff);b2l_nc_p32(actual+300,0xd65f03c0);assert(!b2l_apps_payload_equal(source,sizeof(source),0,actual,sizeof(actual),true));
 fixture();actual[20]^=1;assert(!b2l_apps_payload_equal(source,sizeof(source),0,actual,sizeof(actual),true));
 fixture();assert(!b2l_apps_payload_equal(source,sizeof(source),193,actual+193,3,true));
 assert(!b2l_apps_payload_equal(source,sizeof(source),SIZE_MAX,actual,4,true));
 assert(!b2l_apps_payload_equal(source,sizeof(source),0,actual,SIZE_MAX,true));
 for(unsigned bad=0;bad<10;++bad){fixture();
  switch(bad){case 0:b2l_nc_p32(source,0);break;case 1:b2l_nc_p32(source+8,0);break;
   case 2:b2l_nc_p32(source+16,1025);break;case 3:b2l_nc_p32(source+20,600);break;
   case 4:b2l_nc_p32(source+36,8);break;case 5:b2l_nc_p32(source+96,2);break;
   case 6:b2l_nc_p32(source+92,1);break;case 7:b2l_nc_p32(source+168,0x400);break;
   case 8:b2l_nc_p64(source+144,1024);break;case 9:b2l_nc_p32(source+152,193);break;}
  memcpy(actual,source,sizeof(actual));b2l_nc_p32(actual+192,0xd65f03c0);
  assert(!b2l_apps_payload_equal(source,sizeof(source),0,actual,sizeof(actual),true));
 }
 fixture();b2l_nc_p32(source+192,0x9ac03000);assert(!b2l_apps_payload_equal(source,sizeof(source),0,actual,sizeof(actual),true));
 if(argc==3){size_t a,b;unsigned char*x=load(argv[1],&a),*y=load(argv[2],&b);assert(a==b);
  assert(b2l_apps_payload_equal(x,a,0,y,b,true));assert(!b2l_apps_payload_equal(x,a,0,y,b,false));
  unsigned changed=0;for(size_t i=0;i+4<=a;i+=4)changed+=memcmp(x+i,y+i,4)!=0;
  printf("saved device executable: %u allowlisted code words accepted; strict comparison rejects\n",changed);free(x);free(y);
 }
 puts("actual payload comparator: exact data/header, code-only rewrites, malformed ranges/commands, unknown conversion rejection PASS");
}
'''
a=argparse.ArgumentParser();a.add_argument('--original');a.add_argument('--current');args=a.parse_args()
with tempfile.TemporaryDirectory(prefix='b2l-payload-compare-') as td:
 p=Path(td);(p/'test.c').write_text(pre)
 subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(r/'xnu-overlay/bsd/kern'),'-I'+str(r/'common/cache'),str(p/'test.c'),str(r/'common/cache/b2l_cache_core.c'),str(r/'common/cache/CacheSha.c'),'-o',str(p/'test')],check=True)
 assert bool(args.original)==bool(args.current)
 subprocess.run([str(p/'test')]+([args.original,args.current] if args.original else []),check=True)
