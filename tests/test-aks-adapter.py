#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Compile the actual adapter functions with a bounded descriptor mock."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]; arm=root/'xnu-overlay/iokit/Kernel/arm'
s=(arm/'B2LKeyStore.cpp').read_text();start=s.index('static IOReturn result_code(');end=s.index('IOReturn AppleKeyStoreUserClient::externalMethod',start)
pre=r'''
#include <cassert>
#include <cstring>
#include <cstdint>
#include <climits>
#include <cstdio>
extern "C" {
#include "b2l_aks_core.h"
}
using IOReturn=int;using IOByteCount=size_t;
enum {kIOReturnSuccess=0,kIOReturnUnsupported=1,kIOReturnBadArgument=2,kIOReturnNotOpen=3,kIOReturnNotFound=4,kIOReturnNoSpace=5,kIOReturnError=6};
struct Descriptor {
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
'''
post=r'''
static void empty(IOExternalMethodArguments&a){assert(!a.scalarOutputCount&&!a.structureOutputSize&&!a.structureOutputDescriptorSize);}
int main(){
 b2l_aks_client state; b2l_aks_client_init(&state);IOExternalMethodArguments a;
 assert(b2l_aks_external(&state,0,&a)==kIOReturnSuccess&&state.opened);empty(a);
 uint64_t guarded[3]={0xbadcafe,0xa5a5,0xbadcafe};a.scalarOutput=&guarded[1];a.scalarOutputCount=1;
 assert(b2l_aks_external(&state,14,&a)==0&&a.scalarOutputCount==1&&guarded[1]==0&&guarded[0]==0xbadcafe&&guarded[2]==0xbadcafe);
 uint64_t handle=0;a={};a.scalarInput=&handle;a.scalarInputCount=1;a.scalarOutput=&guarded[1];a.scalarOutputCount=1;
 assert(b2l_aks_external(&state,7,&a)==0&&guarded[1]==4);
 handle=1;a.scalarOutputCount=1;assert(b2l_aks_external(&state,7,&a)==kIOReturnNotFound);empty(a);
 /* The actual adapter must copy only12 DER bytes, even for native4096 capacity. */
 for(unsigned sel=17;sel<=35;sel+=18){
  uint8_t bytes[4098];memset(bytes,0xa5,sizeof bytes);handle=0;
  a={};a.scalarInput=&handle;a.scalarInputCount=1;a.structureOutput=bytes+1;a.structureOutputSize=4096;
  assert(b2l_aks_external(&state,sel,&a)==0&&a.structureOutputSize==12&&!a.scalarOutputCount);
  assert(bytes[0]==0xa5&&bytes[13]==0xa5&&bytes[4097]==0xa5);
  assert(bytes[1]==0x31&&bytes[12]==3);
  a.structureOutputSize=11;assert(b2l_aks_external(&state,sel,&a)==kIOReturnNoSpace);empty(a);
  Descriptor wire;wire.length=12;a={};a.scalarInput=&handle;a.scalarInputCount=1;a.structureOutputDescriptor=&wire;
  assert(b2l_aks_external(&state,sel,&a)==0&&a.structureOutputDescriptorSize==12&&a.structureOutputSize==0);
  wire.shortWrite=true;a.structureOutputDescriptorSize=0;
  assert(b2l_aks_external(&state,sel,&a)==kIOReturnNoSpace);empty(a);
 }
 /* Missing buffers, oversized counts and all async paths fail before dispatch. */
 for(int k=0;k<10;k++){
   a={};a.scalarOutputCount=1;
   switch(k){case 0:break;case 1:a.scalarInputCount=5;break;case 2:a.scalarInputCount=1;break;
   case 3:a.scalarOutputCount=5;break;case 4:a.asyncWakePort=1;break;case 5:a.asyncReferenceCount=1;break;
   case 6:a.structureVariableOutputData=&a;break;case 7:a.structureInputSize=257;break;
   case 8:a.structureInputSize=1;break;case 9:a.scalarOutput=&guarded[1];a.structureInputSize=1;break;}
   assert(b2l_aks_external(&state,14,&a)==kIOReturnBadArgument);empty(a);
 }
 Descriptor d;d.length=1;d.shortRead=true;a={};a.structureInputDescriptor=&d;
 assert(b2l_aks_external(&state,0,&a)==kIOReturnBadArgument);empty(a);
 d.shortRead=false;a={};a.structureInputDescriptor=&d;a.structureInput=&d;
 assert(b2l_aks_external(&state,0,&a)==kIOReturnBadArgument);empty(a);
 a={};a.structureOutputDescriptor=&d;a.structureOutput=&d;
 assert(b2l_aks_external(&state,0,&a)==kIOReturnBadArgument);empty(a);
 d.length=UINT64_MAX;a={};a.structureOutputDescriptor=&d;
 assert(b2l_aks_external(&state,0,&a)==kIOReturnBadArgument);empty(a);
 b2l_aks_client_init(&state);d.length=1;a={};a.structureOutputDescriptor=&d;
 assert(b2l_aks_external(&state,0,&a)==kIOReturnBadArgument&&!state.opened);empty(a);
 a={};assert(b2l_aks_external(nullptr,0,&a)==kIOReturnBadArgument);
 assert(b2l_aks_external(&state,0,nullptr)==kIOReturnBadArgument);
 puts("actual adapter bounds, outputs, descriptors, transactional state: PASS");
}
'''
with tempfile.TemporaryDirectory() as td:
 t=Path(td);(t/'test.cpp').write_text(pre+s[start:end]+post)
 subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(arm),'-c',str(arm/'b2l_aks_core.c'),'-o',str(t/'core.o')],check=True)
 subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(arm),str(t/'test.cpp'),str(t/'core.o'),'-o',str(t/'test')],check=True)
 subprocess.run([str(t/'test')],check=True)
