#!/usr/bin/env python3
"""Execute framebuffer ABI dispatch cases under ASan/UBSan; no hardware claim."""
from pathlib import Path
import subprocess, tempfile
root=Path(__file__).resolve().parents[1]
s=(root/'xnu-overlay/iokit/Kernel/arm/B2LFramebuffer.cpp').read_text()
enums=s[s.index('enum {'):s.index('};',s.index('enum {'))+2]
cases=s[s.index('\tcase kB2LFBFrameInfoDescriptor: {'):s.index('\tcase kB2LFBGetLayerDefaultSurface:',s.index('\tcase kB2LFBFrameInfoDescriptor: {'))]
tail=s[s.index('    if(outCount &&'):s.index('\tif (selector >= 64')]
code=r'''#include <cassert>
#include <cstdint>
#include <cstring>
#include <strings.h>
#include <cstddef>
#include <initializer_list>
using IOReturn=int;
enum { kIOReturnSuccess=0,kIOReturnBadArgument=1,kIOReturnUnsupported=2,kIOReturnNoSpace=3 };
struct Args {
 uint32_t scalarInputCount,scalarOutputCount;
 const uint64_t *scalarInput;uint64_t *scalarOutput;
 size_t structureInputSize,structureOutputSize;
 const void *structureInput;void *structureOutput;
 void *structureInputDescriptor,*structureOutputDescriptor;
};
''' + enums + r'''
static IOReturn call(unsigned selector,Args *args) {
 if (args == NULL) return kIOReturnBadArgument;
 IOReturn ret=kIOReturnSuccess;uint64_t out[2]={0,0};uint32_t outCount=0;
 switch(selector) {
''' + cases + r'''
 default:ret=kIOReturnUnsupported;break;
 }
''' + tail + r'''
 return ret;
}
int main() {
 uint64_t in=0,out[3]={9,9,9};uint8_t buf[66];memset(buf,0xa5,sizeof(buf));
 Args a{1,2,&in,out,0,64,nullptr,buf+1,nullptr,nullptr};
 assert(call(74,&a)==0&&out[0]==0&&out[1]==0&&out[2]==9&&a.structureOutputSize==64);
 assert(buf[0]==0xa5&&buf[65]==0xa5);for(unsigned i=1;i<65;++i)assert(buf[i]==0);
 // Invalid ABI requests must not touch any caller output or length.
 for(unsigned i=0;i<12;++i) {
  Args b{1,2,&in,out,0,64,nullptr,buf+1,nullptr,nullptr};in=0;out[0]=out[1]=9;
  memset(buf,0xa5,sizeof(buf));
  if(i==0)b.scalarInputCount=0;if(i==1)b.scalarInput=nullptr;
  if(i==2)b.structureInputSize=1;if(i==3)b.structureInputDescriptor=(void*)1;
  if(i==4)in=1;if(i==5)b.structureOutputDescriptor=(void*)1;
  if(i==6)b.scalarOutputCount=1;if(i==7)b.scalarOutput=nullptr;
  if(i==8)b.structureOutputSize=63;if(i==9)b.structureOutput=nullptr;
  if(i==10)b.structureOutputSize=65;if(i==11)b.scalarOutputCount=3;
  size_t before=b.structureOutputSize;assert(call(74,&b)!=0);
  assert(out[0]==9&&out[1]==9&&b.structureOutputSize==before);
  for(uint8_t v:buf)assert(v==0xa5);
 }
 uint32_t neutral=0x3f800000;Args contrast{};contrast.structureInput=&neutral;contrast.structureInputSize=4;
 assert(call(84,&contrast)==0);
 for(uint32_t v:{0U,0x3f000000U,0x7fc00000U,0x7f800000U}) {neutral=v;assert(call(84,&contrast)==kIOReturnUnsupported);}
 neutral=0x3f800000;contrast.structureInputSize=3;assert(call(84,&contrast)==kIOReturnBadArgument);
 contrast.structureInputSize=4;contrast.structureInputDescriptor=(void*)1;assert(call(84,&contrast)==kIOReturnBadArgument);
 Args select{};in=0;select.scalarInputCount=1;select.scalarInput=&in;
 assert(call(88,&select)==0);in=1;assert(call(88,&select)==kIOReturnUnsupported);
 in=0;select.scalarOutputCount=1;assert(call(88,&select)==kIOReturnBadArgument);
 select.scalarOutputCount=0;select.scalarInput=nullptr;assert(call(88,&select)==kIOReturnBadArgument);
 assert(call(74,nullptr)==kIOReturnBadArgument);
 Args unknown{};assert(call(68,&unknown)==kIOReturnUnsupported&&call(79,&unknown)==kIOReturnUnsupported);
}
'''
with tempfile.TemporaryDirectory(prefix='b2l-fb-query-') as tmp:
 tmp=Path(tmp);src=tmp/'check.cpp';src.write_text(code);binary=tmp/'check'
 subprocess.run(['clang++','-std=c++17','-g','-O1','-fsanitize=address,undefined','-fno-omit-frame-pointer',str(src),'-o',str(binary)],check=True)
 subprocess.run([str(binary)],check=True)
print('PASS: actual framebuffer descriptor, identity contrast and main display ABI dispatch (ASan/UBSan)')
