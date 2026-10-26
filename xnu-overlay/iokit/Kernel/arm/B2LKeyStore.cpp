/* SPDX-License-Identifier: BSD-2-Clause
 * Bounded IOUserClient adapter for the B2L volatile no-passcode development ABI.
 * Unsupported cryptographic operations never return fabricated wrapped keys.
 */
#ifndef B2L_AKS_HOST_TEST
#include <IOKit/IOService.h>
#include <IOKit/IOUserClient.h>
#include <IOKit/IOKitKeys.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <arm64/b2l_arm64e_compat.h>
#include <sys/codesign.h>
#include <os/atomic_private.h>
#endif
#include "B2LMemoryIO.h"
extern "C" {
#include "b2l_aks_core.h"
}
static bool b2l_aks_enabled(void)
{return b2l_arm64e_compat_enabled()&&!cs_process_global_enforcement();}
/* Globally saturating, concurrency-safe diagnostic budget; never log payloads. */
static void b2l_aks_log(uint32_t selector,IOReturn result)
{
    static uint32_t count;uint32_t n=os_atomic_load(&count,relaxed);
    while(n<24){
        if(os_atomic_cmpxchgv(&count,n,n+1,&n,relaxed)){
            IOLog("B2L AKS: selector=%u result=0x%x\n",selector,(unsigned)result);break;
        }
    }
}
class AppleKeyStore : public IOService {
    OSDeclareDefaultStructors(AppleKeyStore);
public:
    bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
};
OSDefineMetaClassAndStructors(AppleKeyStore,IOService);
bool AppleKeyStore::start(IOService*provider)
{
    if(!b2l_aks_enabled()||!IOService::start(provider))return false;
    setName("AppleKeyStore");
    if(!setProperty(kIOUserClientClassKey,"AppleKeyStoreUserClient")){
        IOService::stop(provider);return false;
    }
    setProperty("B2LDevelopmentVolatileNoPasscode",true);
    registerService();
    IOLog("B2L AKS: volatile no-passcode service registered; no SEP or wrapped-key support\n");
    return true;
}
class AppleKeyStoreUserClient : public IOUserClient {
    OSDeclareDefaultStructors(AppleKeyStoreUserClient);
    IOLock *stateLock;
    struct b2l_aks_client state;
public:
    bool initWithTask(task_t task,void *token,UInt32 type,OSDictionary *properties) APPLE_KEXT_OVERRIDE;
    bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
    void free(void) APPLE_KEXT_OVERRIDE;
    IOReturn clientClose(void) APPLE_KEXT_OVERRIDE;
    IOReturn externalMethod(uint32_t selector,IOExternalMethodArguments *arguments,
        IOExternalMethodDispatch *dispatch,OSObject *target,void *reference) APPLE_KEXT_OVERRIDE;
};
OSDefineMetaClassAndStructors(AppleKeyStoreUserClient,IOUserClient);
bool AppleKeyStoreUserClient::initWithTask(task_t task,void*token,UInt32 type,OSDictionary*properties)
{
    stateLock=nullptr;b2l_aks_client_init(&state);
    if(type!=0||!b2l_aks_enabled()||!IOUserClient::initWithTask(task,token,type,properties))return false;
    stateLock=IOLockAlloc();return stateLock!=nullptr;
}
bool AppleKeyStoreUserClient::start(IOService*provider)
{return OSDynamicCast(AppleKeyStore,provider)&&b2l_aks_enabled()&&IOUserClient::start(provider);}
void AppleKeyStoreUserClient::free(void)
{if(stateLock){IOLockFree(stateLock);stateLock=nullptr;}IOUserClient::free();}
IOReturn AppleKeyStoreUserClient::clientClose(void)
{
    if(stateLock){IOLockLock(stateLock);b2l_aks_client_close(&state);IOLockUnlock(stateLock);}
    terminate();return kIOReturnSuccess;
}
static IOReturn result_code(enum b2l_aks_result result)
{
    switch(result){
    case B2L_AKS_SUCCESS:return kIOReturnSuccess;
    case B2L_AKS_UNSUPPORTED:return kIOReturnUnsupported;
    case B2L_AKS_BAD_ARGUMENT:return kIOReturnBadArgument;
    case B2L_AKS_NOT_OPEN:return kIOReturnNotOpen;
    case B2L_AKS_NOT_FOUND:return kIOReturnNotFound;
    case B2L_AKS_NO_SPACE:return kIOReturnNoSpace;
    }
    return kIOReturnError;
}
static IOReturn b2l_aks_external(struct b2l_aks_client*state,uint32_t selector,IOExternalMethodArguments*a)
{
    if(!state||!a)return kIOReturnBadArgument;
    const uint32_t outScalars=a->scalarOutputCount;
    uint64_t inSize=a->structureInputSize,outSize=a->structureOutputSize;
    if(a->structureInputDescriptor)inSize=a->structureInputDescriptor->getLength();
    if(a->structureOutputDescriptor)outSize=a->structureOutputDescriptor->getLength();
    a->scalarOutputCount=0;a->structureOutputSize=0;a->structureOutputDescriptorSize=0;
    if(a->asyncWakePort||a->asyncReferenceCount||a->structureVariableOutputData||
       a->scalarInputCount>B2L_AKS_MAX_SCALARS||outScalars>B2L_AKS_MAX_SCALARS||
       (a->scalarInputCount&&!a->scalarInput)||(outScalars&&!a->scalarOutput)||
       (a->structureInputDescriptor&&a->structureInput)||
       (a->structureOutputDescriptor&&a->structureOutput)||
       inSize>B2L_AKS_MAX_STRUCTURE||outSize>UINT32_MAX)return kIOReturnBadArgument;
    uint8_t input[B2L_AKS_MAX_STRUCTURE];
    const void*inputPointer=a->structureInput;
    if(inSize){
        if(a->structureInputDescriptor){
            if(b2l_descriptor_read(a->structureInputDescriptor, 0,input,(IOByteCount)inSize)!=inSize)return kIOReturnBadArgument;
            inputPointer=input;
        }else if(!inputPointer)return kIOReturnBadArgument;
    }
    struct b2l_aks_call call={selector,a->scalarInput,a->scalarInputCount,inputPointer,(size_t)inSize,outScalars,(size_t)outSize};
    struct b2l_aks_reply reply;
    struct b2l_aks_client candidate=*state;
    IOReturn result=result_code(b2l_aks_dispatch(&candidate,&call,&reply));
    if(result!=kIOReturnSuccess)return result;
    if(reply.scalar_count>outScalars||reply.structure_size>outSize)return kIOReturnNoSpace;
    if(reply.structure_size){
        if(a->structureOutputDescriptor){
            if(b2l_descriptor_write(a->structureOutputDescriptor, 0,reply.structure,reply.structure_size)!=reply.structure_size)return kIOReturnNoSpace;
            a->structureOutputDescriptorSize=(uint32_t)reply.structure_size;
        }else{
            if(!a->structureOutput)return kIOReturnBadArgument;
            memcpy(a->structureOutput,reply.structure,reply.structure_size);
            a->structureOutputSize=(uint32_t)reply.structure_size;
        }
    }
    for(size_t i=0;i<reply.scalar_count;i++)a->scalarOutput[i]=reply.scalars[i];
    a->scalarOutputCount=(uint32_t)reply.scalar_count;
    *state=candidate;
    return kIOReturnSuccess;
}
IOReturn AppleKeyStoreUserClient::externalMethod(uint32_t selector,IOExternalMethodArguments*a,
    IOExternalMethodDispatch*dispatch __unused,OSObject*target __unused,void*reference __unused)
{
    if(!stateLock||!b2l_aks_enabled()){
        if(a){a->scalarOutputCount=0;a->structureOutputSize=0;a->structureOutputDescriptorSize=0;}
        return kIOReturnNotOpen;
    }
    IOLockLock(stateLock);
    IOReturn result=b2l_aks_external(&state,selector,a);
    IOLockUnlock(stateLock);
    b2l_aks_log(selector,result);
    return result;
}
