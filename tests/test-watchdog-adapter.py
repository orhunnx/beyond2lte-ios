#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Exercise the real watchdog adapter with guarded buffers and descriptor failures."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
s=(root/'xnu-overlay/iokit/Kernel/arm/B2LWatchdog.cpp').read_text()
s=s[s.index('static IOReturn b2l_wdt_call'):s.index('IOReturn IOWatchdogUserClient::externalMethod')]
pre=r'''
#include <cassert>
#include <cstring>
#include <cstdint>
#include <climits>
#include <cstdio>
using IOReturn=int;using IOByteCount=size_t;
enum {kIOReturnSuccess=0,kIOReturnUnsupported=1,kIOReturnBadArgument=2,kIOReturnNotOpen=3,kIOReturnNotFound=4,kIOReturnNoSpace=5,kIOReturnError=6};
struct Descriptor { int prepare(int){return 0;} int complete(int){return 0;}
 uint8_t data[256]={};size_t length=0;bool shortRead=false,shortWrite=false;
 size_t getLength(){return length;}
 size_t readBytes(size_t,void*out,size_t n){if(shortRead)return 0;assert(n<=256);memcpy(out,data,n);return n;}
 size_t writeBytes(size_t,const void*in,size_t n){if(shortWrite)return 0;assert(n<=256);memcpy(data,in,n);return n;}
};
struct IOExternalMethodArguments {
 uint32_t asyncWakePort=0,asyncReferenceCount=0;void*structureVariableOutputData=nullptr;
 const uint64_t*scalarInput=nullptr;uint32_t scalarInputCount=0;
 uint64_t*scalarOutput=nullptr;uint32_t scalarOutputCount=0;
 const void*structureInput=nullptr;uint32_t structureInputSize=0;
 void*structureOutput=nullptr;uint32_t structureOutputSize=0;
 Descriptor*structureInputDescriptor=nullptr,*structureOutputDescriptor=nullptr;
 uint32_t structureOutputDescriptorSize=0;
};

#include <initializer_list>
'''
pre+='\nenum {kIODirectionIn=1,kIODirectionOut=2};\n'+(root/'xnu-overlay/iokit/Kernel/arm/B2LMemoryIO.h').read_text()+'\n'
post=r'''
int main(){
 IOExternalMethodArguments a;
 assert(b2l_wdt_call(1,nullptr)==kIOReturnBadArgument);
 for(unsigned selector: {1U,3U,4U,6U,8U}){a={};assert(b2l_wdt_call(selector,&a)==0);}
 for(unsigned selector: {0U,5U}){
  uint64_t v[3]={0xabc,99,0xdef};a={};a.scalarOutput=&v[1];a.scalarOutputCount=1;
  assert(b2l_wdt_call(selector,&a)==0&&v[1]==0&&v[0]==0xabc&&v[2]==0xdef&&a.scalarOutputCount==1);
 }
 uint64_t stage=42;a={};a.scalarInput=&stage;a.scalarInputCount=1;assert(b2l_wdt_call(7,&a)==0);
 char input[2048]={};a={};a.structureInput=input;a.structureInputSize=sizeof(input);assert(b2l_wdt_call(2,&a)==0);
 uint8_t guarded[4]={0xaa,0xbb,0xcc,0xdd};a={};a.structureOutput=guarded+1;a.structureOutputSize=2;
 assert(b2l_wdt_call(9,&a)==0&&a.structureOutputSize==2&&guarded[0]==0xaa&&guarded[1]==0&&guarded[2]==0&&guarded[3]==0xdd);
 Descriptor d;d.length=2;a={};a.structureOutputDescriptor=&d;
 assert(b2l_wdt_call(9,&a)==0&&a.structureOutputDescriptorSize==2);
 d.shortWrite=true;a.structureOutputDescriptor=&d;assert(b2l_wdt_call(9,&a)==kIOReturnNoSpace);
 for(int i=0;i<10;i++){
  a={};switch(i){case 0:a.asyncWakePort=1;break;case 1:a.asyncReferenceCount=1;break;
   case 2:a.structureVariableOutputData=&a;break;case 3:a.scalarInputCount=1;break;
   case 4:a.scalarOutputCount=1;break;case 5:a.structureInputSize=2049;break;
   case 6:a.structureOutputSize=3;break;case 7:a.structureInputSize=1;break;
   case 8:a.structureOutputSize=2;break;case 9:a.structureInputDescriptor=&d;a.structureInput=input;break;}
  assert(b2l_wdt_call(1,&a)==kIOReturnBadArgument);
  assert(!a.scalarOutputCount&&!a.structureOutputSize&&!a.structureOutputDescriptorSize);
 }
 a={};assert(b2l_wdt_call(10,&a)==kIOReturnUnsupported);assert(b2l_wdt_call(UINT32_MAX,&a)==kIOReturnUnsupported);
 puts("watchdog actual adapter: observed ABI, malformed calls, bounded descriptor outputs PASS");
}
'''
with tempfile.TemporaryDirectory() as td:
 p=Path(td);(p/'test.cpp').write_text(pre+s+post)
 subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
