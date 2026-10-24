#!/usr/bin/env python3
# iOS26 wire ABI regression against actual legacy struct and portable codec.
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[1];d=r/'xnu-overlay/iokit/Kernel/arm/hid'
s=(d/'IOHIDEventStructDefs.h').read_text();at=s.index('uint32_t transducerIndex;');a=s.rindex('typedef struct {',0,at);b=s.index('} IOHIDDigitizerEventData;',at)+len('} IOHIDDigitizerEventData;');digitizer=s[a:b]
code=r'''
#include <cstdint>
#include <cstring>
#include <cassert>
#include <cstdio>
#include <cmath>
using IOFixed=int32_t;using IOHIDEventType=uint32_t;
const uint32_t kIOHIDEventTypeDigitizer=11;
struct IOHIDEventData{uint32_t size,type,options;uint8_t depth,reserved[3];};
'''+digitizer+'\n#include "'+str(d/'b2l_hid_wire.h')+'"\n'+r'''
int main(){
 static_assert(sizeof(IOHIDDigitizerEventData)==104,"legacy ABI reproduced");
 IOHIDDigitizerEventData v={};v.size=sizeof(v);v.type=11;v.depth=1;
 v.position.x=32768;v.position.y=65536;v.position.z=-16384;v.transducerIndex=3;v.transducerType=2;
 v.identity=5;v.eventMask=0x37;v.childEventMask=0x18;v.buttonMask=1;v.pressure=65536;v.generationCount=0x12345678;
 struct {uint8_t before[7],bytes[160],after[9];} guard;memset(&guard,0xa5,sizeof(guard));
 assert(!b2l_hid_wire_write((IOHIDEventData*)&v,guard.bytes,159));
 assert(b2l_hid_wire_size((IOHIDEventData*)&v)==160);
 assert(b2l_hid_wire_write((IOHIDEventData*)&v,guard.bytes,160)==160);
 for(auto c:guard.before)assert(c==0xa5);for(auto c:guard.after)assert(c==0xa5);
 uint32_t generation;memcpy(&generation,guard.bytes+0x90,4);assert(generation==0x12345678);
 double x,y,z;memcpy(&x,guard.bytes+16,8);memcpy(&y,guard.bytes+24,8);memcpy(&z,guard.bytes+32,8);
 assert(x==0.5&&y==1.0&&z==-0.25);
 IOHIDDigitizerEventData restored={};assert(b2l_hid_wire_read(guard.bytes,160,(IOHIDEventData*)&restored));
 assert(!memcmp(&v,&restored,sizeof(v)));assert(!b2l_hid_wire_read(guard.bytes,159,(IOHIDEventData*)&restored));
 for(int64_t n=-2147483647;n<=2147483647;n+=123457){
  uint64_t bits=b2l_fixed_double((int32_t)n);double actual;memcpy(&actual,&bits,8);
  assert(actual==(double)n/65536.0);assert(b2l_double_fixed(bits)==n);
 }
 assert(b2l_double_fixed(b2l_fixed_double(INT32_MIN))==INT32_MIN);
 IOHIDEventData keyboard={32,3,0,0,{}};uint8_t k[32]={};memcpy(k,&keyboard,sizeof(keyboard));uint8_t output[32];
 assert(b2l_hid_wire_write((IOHIDEventData*)k,output,32)==32&&!memcmp(k,output,32));
 puts("iOS26 digitizer: native 160-byte/0x90 ABI, IEEE754 coordinates, round-trip, guards and unchanged integer event bytes PASS");
}
'''
with tempfile.TemporaryDirectory(prefix='b2l-hid-wire-') as td:
 p=Path(td);(p/'t.cpp').write_text(code)
 subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'t.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
