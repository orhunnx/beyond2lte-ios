#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Compile complete production C++ class implementation with a kernel API mock."""
from pathlib import Path
import tempfile,subprocess
r=Path(__file__).resolve().parents[1];arm=r/'xnu-overlay/iokit/Kernel/arm'
pre=r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <climits>
#include <cstdio>
#define B2L_AKS_HOST_TEST 1
#define APPLE_KEXT_OVERRIDE override
#define OSDeclareDefaultStructors(T)
#define OSDefineMetaClassAndStructors(T,B)
#define OSDynamicCast(T,p) dynamic_cast<T*>(p)
#ifndef __unused
#define __unused __attribute__((unused))
#endif
#define os_atomic_load(p,order) (*(p))
#define os_atomic_cmpxchgv(p,o,n,out,order) ((*(p)==(o))?(*(p)=(n),true):(*(out)=*(p),false))
enum {kIODirectionIn=1,kIODirectionOut=2};
using IOReturn=int;using IOByteCount=size_t;using UInt32=uint32_t;using task_t=void*;
enum {kIOReturnSuccess=0,kIOReturnUnsupported=1,kIOReturnBadArgument=2,kIOReturnNotOpen=3,kIOReturnNotFound=4,kIOReturnNoSpace=5,kIOReturnError=6};
static bool compat=true,enforcement=false,baseInit=true,allocFailure=false;
static unsigned held=0,allocated=0,freed=0;
bool b2l_arm64e_compat_enabled(){return compat;}
bool cs_process_global_enforcement(){return enforcement;}
void IOLog(const char*,...){}
struct IOLock{};
IOLock*IOLockAlloc(){if(allocFailure)return nullptr;allocated++;return new IOLock;}
void IOLockFree(IOLock*p){freed++;delete p;}
void IOLockLock(IOLock*){assert(!held);held++;}
void IOLockUnlock(IOLock*){assert(held==1);held--;}
struct Descriptor{ int prepare(int){return 0;} int complete(int){return 0;}uint8_t data[256]={};size_t length=0;size_t getLength(){return length;}
 size_t readBytes(size_t,void*p,size_t n){memcpy(p,data,n);return n;}
 size_t writeBytes(size_t,const void*p,size_t n){memcpy(data,p,n);return n;}};
struct IOExternalMethodArguments{
 uint32_t asyncWakePort=0,asyncReferenceCount=0;void*structureVariableOutputData=nullptr;
 const uint64_t*scalarInput=nullptr;uint32_t scalarInputCount=0;
 uint64_t*scalarOutput=nullptr;uint32_t scalarOutputCount=0;
 const void*structureInput=nullptr;uint32_t structureInputSize=0;
 void*structureOutput=nullptr;uint32_t structureOutputSize=0;
 Descriptor*structureInputDescriptor=nullptr,*structureOutputDescriptor=nullptr;
 uint32_t structureOutputDescriptorSize=0;
};
struct OSObject{virtual ~OSObject()=default;};struct OSDictionary:OSObject{};struct IOExternalMethodDispatch{};
static const char*kIOUserClientClassKey="IOUserClientClass";
class IOService:public OSObject{public:
 virtual bool start(IOService*){return true;}virtual void stop(IOService*){}
 void setName(const char*){}bool setProperty(const char*,const char*){return true;}
 bool setProperty(const char*,bool){return true;}void registerService(){}
};
class IOUserClient:public IOService{public:
 virtual bool initWithTask(task_t,void*,UInt32,OSDictionary*){return baseInit;}
 virtual void free(){}virtual IOReturn clientClose(){return 0;}
 virtual IOReturn externalMethod(uint32_t,IOExternalMethodArguments*,IOExternalMethodDispatch*,OSObject*,void*){return 0;}
 void terminate(){}
};
'''
post=r'''
int main(){
 AppleKeyStore service;IOService provider;
 compat=false;assert(!service.start(&provider));compat=true;enforcement=true;assert(!service.start(&provider));enforcement=false;assert(service.start(&provider));
 AppleKeyStoreUserClient bad;
 assert(!bad.initWithTask(nullptr,nullptr,1,nullptr));bad.free();assert(!allocated);
 compat=false;assert(!bad.initWithTask(nullptr,nullptr,0,nullptr));bad.free();compat=true;
 enforcement=true;assert(!bad.initWithTask(nullptr,nullptr,0,nullptr));bad.free();enforcement=false;
 baseInit=false;assert(!bad.initWithTask(nullptr,nullptr,0,nullptr));bad.free();baseInit=true;
 allocFailure=true;assert(!bad.initWithTask(nullptr,nullptr,0,nullptr));bad.free();allocFailure=false;
 AppleKeyStoreUserClient uc;assert(uc.initWithTask(nullptr,nullptr,0,nullptr));
 assert(!uc.start(&provider));assert(uc.start(&service));
 IOExternalMethodArguments a;assert(uc.externalMethod(0,&a,nullptr,nullptr,nullptr)==0);
 uint64_t out=0xbad;a.scalarOutput=&out;a.scalarOutputCount=1;
 assert(uc.externalMethod(14,&a,nullptr,nullptr,nullptr)==0&&out==0);
 enforcement=true;a.scalarOutputCount=1;a.structureOutputSize=1;a.structureOutputDescriptorSize=1;
 assert(uc.externalMethod(14,&a,nullptr,nullptr,nullptr)==kIOReturnNotOpen&&!a.scalarOutputCount&&!a.structureOutputSize&&!a.structureOutputDescriptorSize);
 enforcement=false;assert(uc.clientClose()==0);a={};assert(uc.externalMethod(0,&a,nullptr,nullptr,nullptr)==kIOReturnNotOpen);
 uc.free();assert(allocated==1&&freed==1&&!held);
 puts("complete actual IOService/client gate, type, failure, lifecycle: PASS");
}
'''
with tempfile.TemporaryDirectory() as td:
 t=Path(td);(t/'test.cpp').write_text(pre+'\n#include "'+str(arm/'B2LKeyStore.cpp')+'"\n'+post)
 subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(arm),'-c',str(arm/'b2l_aks_core.c'),'-o',str(t/'core.o')],check=True)
 subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(arm),str(t/'test.cpp'),str(t/'core.o'),'-o',str(t/'test')],check=True)
 subprocess.run([str(t/'test')],check=True)
