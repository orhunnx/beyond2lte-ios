#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Compile the complete AMFI IOService/client and check gates, ABI and hash pins."""
from pathlib import Path
import argparse,hashlib,plistlib,re,struct,subprocess,tempfile
root=Path(__file__).resolve().parents[1];arm=root/'xnu-overlay/iokit/Kernel/arm'
pre=r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <climits>
#define B2L_AMFI_IOKIT_HOST_TEST 1
#define APPLE_KEXT_OVERRIDE override
#define OSDeclareDefaultStructors(T)
#define OSDefineMetaClassAndStructors(T,B)
#define OSDynamicCast(T,p) dynamic_cast<T*>(p)
#ifndef __unused
#define __unused __attribute__((unused))
#endif
using IOReturn=int;using UInt32=uint32_t;using task_t=void*;using proc_t=void*;
enum {kIOReturnSuccess=0,kIOReturnUnsupported=1,kIOReturnBadArgument=2,
 kIOReturnNotPermitted=3,kIOReturnNotFound=4,kIOReturnNoSpace=5};
static bool active=true,platform=true,hasProcess=true,baseInit=true,baseStart=true,propertyOK=true;
static unsigned registered=0,stopped=0,terminated=0;
static task_t callingTask=(void*)1;
bool b2l_amfi_active(){return active;}
bool b2l_amfi_is_platform(proc_t p){return p&&platform;}
void*get_bsdtask_info(task_t t){return t&&hasProcess?t:nullptr;}
task_t current_task(){return callingTask;}
int proc_selfpid(){return 106;}
void IOLog(const char*,...){}
static unsigned entRefs=0;
struct OSObject{virtual ~OSObject()=default;void release(){assert(entRefs);entRefs--;}};struct OSDictionary:OSObject{};
#define OSSafeReleaseNULL(p) do{if(p){(p)->release();(p)=nullptr;}}while(0)
struct OSBoolean:OSObject{};static OSBoolean trueObject,falseObject;
static OSObject notBoolean;static OSObject*entitlementObject=&trueObject;
static OSBoolean*kOSBooleanTrue=&trueObject;

struct Descriptor {
 uint8_t data[24]={};uint64_t length=20;bool shortRead=false;
 uint64_t getLength(){return length;}
 size_t readBytes(size_t off,void*out,size_t n){assert(!off&&n<=sizeof(data));if(shortRead)return n-1;memcpy(out,data,n);return n;}
};
struct IOExternalMethodArguments{
 uint32_t asyncWakePort=0,asyncReferenceCount=0;void*structureVariableOutputData=nullptr;
 const uint64_t*scalarInput=nullptr;uint32_t scalarInputCount=0;
 uint64_t*scalarOutput=nullptr;uint32_t scalarOutputCount=0;
 const void*structureInput=nullptr;uint32_t structureInputSize=0;
 void*structureOutput=nullptr;uint32_t structureOutputSize=0;
 Descriptor*structureInputDescriptor=nullptr,*structureOutputDescriptor=nullptr;
 uint32_t structureOutputDescriptorSize=0;
};
struct IOExternalMethodDispatch{};static const char*kIOUserClientClassKey="IOUserClientClass";
class IOService:public OSObject{public:
 virtual bool start(IOService*){return baseStart;}
 virtual void stop(IOService*){stopped++;}
 void setName(const char*s){assert(!strcmp(s,"AppleMobileFileIntegrity"));}
 bool setProperty(const char*,const char*){return propertyOK;}
 bool setProperty(const char*,bool){return propertyOK;}
 void registerService(){registered++;}
};
class IOUserClient:public IOService{public:
 virtual bool initWithTask(task_t,void*,UInt32,OSDictionary*){return baseInit;}
 virtual IOReturn clientClose(){return 0;}
 virtual IOReturn externalMethod(uint32_t,IOExternalMethodArguments*,IOExternalMethodDispatch*,OSObject*,void*){return 0;}
 static OSObject*copyClientEntitlement(task_t,const char*s){assert(!strcmp(s,"com.apple.private.amfi.can-check-trust-cache"));if(entitlementObject)entRefs++;return entitlementObject;}
 void terminate(){terminated++;}
};
'''
post=r'''
static uint64_t flag=1;
static IOExternalMethodArguments valid(const void*hash){
 IOExternalMethodArguments a;a.scalarInput=&flag;a.scalarInputCount=1;
 a.structureInput=hash;a.structureInputSize=20;return a;
}
static IOReturn invoke(B2LAMFIUserClient&u,IOExternalMethodArguments&a,unsigned sel=6){return u.externalMethod(sel,&a,nullptr,nullptr,nullptr);}
static void empty(const IOExternalMethodArguments&a){assert(!a.scalarOutputCount&&!a.structureOutputSize&&!a.structureOutputDescriptorSize);}
int main(){
 AppleMobileFileIntegrity service;IOService provider;
 active=false;assert(!service.start(&provider)&&registered==0);active=true;
 baseStart=false;assert(!service.start(&provider));baseStart=true;
 propertyOK=false;assert(!service.start(&provider)&&stopped==1&&registered==0);propertyOK=true;
 assert(service.start(&provider)&&registered==1);
 B2LAMFIUserClient bad;
 assert(!bad.initWithTask(nullptr,nullptr,0,nullptr));
 assert(!bad.initWithTask(callingTask,nullptr,1,nullptr));
 active=false;assert(!bad.initWithTask(callingTask,nullptr,0,nullptr));active=true;
 platform=false;assert(!bad.initWithTask(callingTask,nullptr,0,nullptr));platform=true;
 hasProcess=false;assert(!bad.initWithTask(callingTask,nullptr,0,nullptr));hasProcess=true;
 entitlementObject=nullptr;assert(!bad.initWithTask(callingTask,nullptr,0,nullptr));
 entitlementObject=&falseObject;assert(!bad.initWithTask(callingTask,nullptr,0,nullptr));
 entitlementObject=&notBoolean;assert(!bad.initWithTask(callingTask,nullptr,0,nullptr));entitlementObject=&trueObject;
 baseInit=false;assert(!bad.initWithTask(callingTask,nullptr,0,nullptr));baseInit=true;
 B2LAMFIUserClient u;assert(u.initWithTask(callingTask,nullptr,0,nullptr));
 auto a=valid(b2l_extension_cache_cdhashes[0]);assert(invoke(u,a)==kIOReturnNotPermitted);
 assert(!u.start(&provider));assert(u.start(&service));
 for(auto &hash:b2l_extension_cache_cdhashes){
  uint8_t guarded[22];guarded[0]=0xaa;guarded[21]=0xbb;memcpy(guarded+1,hash,20);
  a=valid(guarded+1);assert(invoke(u,a)==0);empty(a);assert(guarded[0]==0xaa&&guarded[21]==0xbb);
  // Every one-bit mutation must fail; no generic CDHash admission.
  for(unsigned byte=0;byte<20;byte++)for(unsigned bit=0;bit<8;bit++){
   guarded[byte+1]^=(1u<<bit);a=valid(guarded+1);assert(invoke(u,a)==kIOReturnNotFound);guarded[byte+1]^=(1u<<bit);
  }
  Descriptor d;memcpy(d.data,hash,20);a=valid(nullptr);a.structureInputSize=0;a.structureInputDescriptor=&d;
  assert(invoke(u,a)==0);d.shortRead=true;assert(invoke(u,a)==kIOReturnBadArgument);
 }
 uint8_t unknown[20]={};a=valid(unknown);assert(invoke(u,a)==kIOReturnNotFound);
 // Real synchronous MIG shape: non-null backing arrays, zero output counts.
 uint64_t unusedScalar=0xbadcafe;uint8_t unusedBytes[4]={1,2,3,4};
 for(auto &hash:b2l_extension_cache_cdhashes){
  a=valid(hash);a.scalarOutput=&unusedScalar;a.structureOutput=unusedBytes;
  assert(invoke(u,a)==0);empty(a);assert(unusedScalar==0xbadcafe&&unusedBytes[0]==1&&unusedBytes[3]==4);
 }
 a=valid(unknown);a.scalarOutput=&unusedScalar;a.structureOutput=unusedBytes;
 assert(invoke(u,a)==kIOReturnNotFound);empty(a);
 Descriptor migDescriptor;memcpy(migDescriptor.data,b2l_extension_cache_cdhashes[0],20);
 a=valid(unknown);a.structureInputSize=0;a.structureInputDescriptor=&migDescriptor;
 a.scalarOutput=&unusedScalar;a.structureOutput=unusedBytes;
 assert(invoke(u,a)==0);empty(a);
 for(int i=0;i<22;i++){
  a=valid(b2l_extension_cache_cdhashes[0]);Descriptor d;uint64_t out=0xbadcafe;uint8_t outbytes[4]={1,2,3,4};
  switch(i){case 0:a.asyncWakePort=1;break;case 1:a.asyncReferenceCount=1;break;
  case 2:a.structureVariableOutputData=&a;break;case 3:a.scalarInputCount=0;break;
  case 4:a.scalarInputCount=2;break;case 5:a.scalarInput=nullptr;break;
  case 6:flag=2;break;case 7:a.scalarOutput=&out;a.scalarOutputCount=1;break;
  case 8:a.structureOutput=outbytes;a.structureOutputSize=4;break;case 9:a.structureOutputDescriptor=&d;break;
  case 10:a.structureInput=nullptr;break;case 11:a.structureInputSize=0;break;case 12:a.structureInputSize=19;break;
  case 13:a.structureInputSize=21;break;case 14:a.structureInputSize=UINT32_MAX;break;
  case 15:a.structureInputDescriptor=&d;break;case 16:a.structureInputDescriptor=&d;a.structureInput=nullptr;break;
  case 17:d.length=21;a.structureInputDescriptor=&d;a.structureInput=nullptr;a.structureInputSize=0;break;
  case 18:d.length=UINT64_MAX;a.structureInputDescriptor=&d;a.structureInput=nullptr;a.structureInputSize=0;break;
  case 19:a.structureOutputSize=1;break;case 20:a.scalarOutputCount=1;break;
  case 21:a.structureOutputDescriptorSize=1;break;}
  assert(invoke(u,a)==kIOReturnBadArgument);empty(a);assert(out==0xbadcafe&&outbytes[0]==1&&outbytes[3]==4);flag=1;
 }
 a=valid(b2l_extension_cache_cdhashes[0]);assert(invoke(u,a,5)==kIOReturnUnsupported);assert(invoke(u,a,UINT32_MAX)==kIOReturnUnsupported);
 assert(u.externalMethod(6,nullptr,nullptr,nullptr,nullptr)==kIOReturnBadArgument);
 for(unsigned gate=0;gate<5;gate++){
  a=valid(b2l_extension_cache_cdhashes[0]);a.scalarOutputCount=a.structureOutputSize=a.structureOutputDescriptorSize=1;
  switch(gate){case 0:active=false;break;case 1:platform=false;break;case 2:hasProcess=false;break;case 3:entitlementObject=nullptr;break;case 4:callingTask=nullptr;break;}
  assert(invoke(u,a)==kIOReturnNotPermitted);empty(a);active=platform=hasProcess=true;entitlementObject=&trueObject;callingTask=(void*)1;
 }
 assert(u.clientClose()==0&&terminated==1);a=valid(b2l_extension_cache_cdhashes[0]);assert(invoke(u,a)==kIOReturnNotPermitted);
 assert(entRefs==0);
 puts("actual AMFI IOService/client: service lifecycle, gates, 2 pins, 320 hash mutations, descriptor/call bounds PASS");
}
'''
def check_provenance(plist,signature):
 data=Path(plist).read_bytes();sig=Path(signature).read_bytes()
 assert hashlib.sha256(data).hexdigest()=='8f4d7b1576731c56eb8e67b3a9029417aace6c66100244c9ba96a78eb0f48cd1'
 assert hashlib.sha256(sig).hexdigest()=='a3fac9d4d5511df7a27e2482a47c7cc8514ace6d731929b665daa40cfb7dfbfd'
 magic,length,count=struct.unpack_from('>III',sig);assert magic==0xfade0cc0 and length==len(sig)
 digests=[]
 for i in range(count):
  slot,offset=struct.unpack_from('>II',sig,12+8*i);magic,length=struct.unpack_from('>II',sig,offset)
  if magic!=0xfade0c02:continue
  cd=sig[offset:offset+length];ho,ident,special,pages,limit=struct.unpack_from('>5I',cd,16);hs,ht,platform,ps=cd[36:40];algorithm={1:'sha1',2:'sha256'}[ht]
  assert limit==len(data) and pages==24 and ho+pages*hs==len(cd)
  for j in range(pages):assert hashlib.new(algorithm,data[j*(1<<ps):min((j+1)*(1<<ps),limit)]).digest()[:hs]==cd[ho+j*hs:ho+(j+1)*hs]
  digests.append(hashlib.new(algorithm,cd).digest()[:20])
 pins=(arm/'b2l_amfi_cache_pins.h').read_text();actual=[bytes(int(x,16) for x in re.findall(r'0x([0-9a-f]{2})',line)) for line in pins.splitlines() if line.lstrip().startswith('{')]
 assert digests==actual
 obj=plistlib.loads(data);assert len(obj)==126 and all(x in obj for x in ['com.apple.share-services','com.apple.ui-services','com.apple.services'])
 print('exact IPSW/device cache pair, 2 CodeDirectories/24 page hashes, compiled pins, 126 definitions PASS')
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--plist');p.add_argument('--signature');a=p.parse_args()
 with tempfile.TemporaryDirectory(prefix='b2l-amfi-io-') as td:
  t=Path(td);(t/'test.cpp').write_text(pre+'\n#include "'+str(arm/'B2LAMFI.cpp')+'"\n'+post)
  subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(t/'test.cpp'),'-o',str(t/'test')],check=True)
  subprocess.run([str(t/'test')],check=True)
 if a.plist or a.signature:
  if not a.plist or not a.signature:p.error('both provenance paths are required')
  check_provenance(a.plist,a.signature)
