#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Exercise actual pin/copy/unpin helpers with dirty, prepared and failing MDs."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[1]
pre=r"""
#include <cassert>
#include <cstdint>
#include <cstring>
#include <climits>
using IOReturn=int;
enum{kIOReturnSuccess=0,kIODirectionIn=1,kIODirectionOut=2};
struct Descriptor {
 uint8_t data[32]{};uint64_t length=32;int wires=0,prepares=0,completes=0,copies=0,direction=0;
 bool fail_prepare=false,fail_complete=false,short_copy=false;
 uint64_t getLength(){return length;}
 int prepare(int d){++prepares;direction=d;if(fail_prepare)return 1;++wires;return 0;}
 int complete(int d){assert(d==direction&&wires);++completes;--wires;return fail_complete?1:0;}
 uint64_t readBytes(uint64_t at,void*p,uint64_t n){assert(wires&&direction==kIODirectionOut);++copies;memcpy(p,data+at,n);return short_copy?n-1:n;}
 uint64_t writeBytes(uint64_t at,const void*p,uint64_t n){assert(wires&&direction==kIODirectionIn);++copies;memcpy(data+at,p,n);return short_copy?n-1:n;}
};
"""
post=r"""
int main(){
 uint8_t guarded[18];memset(guarded,0xa5,sizeof(guarded));
 for(bool reading:{false,true})for(int initial:{0,2})for(unsigned fault=0;fault<4;++fault){
  Descriptor md;md.wires=initial;md.fail_prepare=fault==1;md.fail_complete=fault==2;md.short_copy=fault==3;
  uint64_t n=reading?b2l_descriptor_read(&md,4,guarded+1,16):b2l_descriptor_write(&md,4,guarded+1,16);
  assert(md.prepares==1&&md.completes==(fault==1?0:1)&&md.copies==(fault==1?0:1)&&md.wires==initial);
  assert(n==(fault==1||fault==2?0:fault==3?15:16));
  assert(guarded[0]==0xa5&&guarded[17]==0xa5);
 }
 for(unsigned fault=0;fault<6;++fault){Descriptor md;uint64_t at=0,n=16;void*p=guarded+1;
  if(fault==0)at=UINT64_MAX;if(fault==1)n=UINT64_MAX;if(fault==2)at=33;
  if(fault==3)n=0;if(fault==4)p=nullptr;if(fault==5){at=24;n=16;}
  assert(!b2l_descriptor_read(&md,at,p,n)&&!b2l_descriptor_write(&md,at,p,n));assert(!md.prepares&&!md.completes&&!md.copies);
 }
 Descriptor*none=nullptr;assert(!b2l_descriptor_read(none,0,guarded,1));assert(!b2l_descriptor_write(none,0,guarded,1));
}
"""
with tempfile.TemporaryDirectory(prefix='b2l-memory-io-') as td:
 p=Path(td);src=p/'test.cpp';src.write_text('#include <initializer_list>\n'+pre+(r/'xnu-overlay/iokit/Kernel/arm/B2LMemoryIO.h').read_text()+post)
 subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(src),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('actual IOKit descriptor helpers: wire lifetime, nested prepare, failed prepare/complete, short copy, overflow/bounds PASS')
