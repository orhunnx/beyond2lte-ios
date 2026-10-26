/* SPDX-License-Identifier: BSD-2-Clause
 * Development-only watchdogd ABI, adapted from the A5/SWEET service.
 * No hardware timer is armed and no reset protection is provided.
 */
#include <IOKit/IOService.h>
#include <IOKit/IOUserClient.h>
#include <IOKit/IOKitKeys.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <libkern/c++/OSDictionary.h>
#include <libkern/c++/OSNumber.h>
#include <libkern/c++/OSString.h>
#include <arm64/b2l_arm64e_compat.h>
#include <sys/codesign.h>
#include <os/atomic_private.h>
static bool b2l_wdt_enabled(void)
{return b2l_arm64e_compat_enabled()&&!cs_process_global_enforcement();}
class IOWatchdog : public IOService {
    OSDeclareDefaultStructors(IOWatchdog);
public:
    bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
    IOReturn setProperties(OSObject *properties) APPLE_KEXT_OVERRIDE;
};
#include "B2LMemoryIO.h"

OSDefineMetaClassAndStructors(IOWatchdog,IOService);
bool IOWatchdog::start(IOService *provider)
{
    if(!b2l_wdt_enabled()||!IOService::start(provider))return false;
    if(!setProperty(kIOUserClientClassKey,"IOWatchdogUserClient")){
        IOService::stop(provider);return false;
    }
    setProperty("IOPMUPrimary",1ULL,32);
    setProperty("B2LDevelopmentNoHardwareWatchdog",true);
    registerService();
    IOLog("B2L WDT: IOWatchdog registered; check-ins only, no hardware reset\n");
    return true;
}
IOReturn IOWatchdog::setProperties(OSObject *properties)
{
    if(!b2l_wdt_enabled())return kIOReturnUnsupported;
    OSDictionary *d=OSDynamicCast(OSDictionary,properties);
    if(!d||d->getCount()!=1)return kIOReturnBadArgument;
    OSObject *stage=d->getObject("IOPMUBootStage");
    if(stage&&OSDynamicCast(OSNumber,stage))
        return setProperty("IOPMUBootStage",stage)?kIOReturnSuccess:kIOReturnNoMemory;
    OSString *app=OSDynamicCast(OSString,d->getObject("IOPMUBootAppName"));
    if(app&&app->getLength()<=128)
        return setProperty("IOPMUBootAppName",app)?kIOReturnSuccess:kIOReturnNoMemory;
    return kIOReturnUnsupported;
}
class IOWatchdogUserClient : public IOUserClient {
    OSDeclareDefaultStructors(IOWatchdogUserClient);
public:
    bool initWithTask(task_t task,void *token,UInt32 type,OSDictionary *properties) APPLE_KEXT_OVERRIDE;
    bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
    IOReturn clientClose(void) APPLE_KEXT_OVERRIDE;
    IOReturn externalMethod(uint32_t selector,IOExternalMethodArguments *a,
        IOExternalMethodDispatch *dispatch,OSObject *target,void *reference) APPLE_KEXT_OVERRIDE;
};
OSDefineMetaClassAndStructors(IOWatchdogUserClient,IOUserClient);
bool IOWatchdogUserClient::initWithTask(task_t task,void *token,UInt32 type,OSDictionary *properties)
{return type==1&&b2l_wdt_enabled()&&IOUserClient::initWithTask(task,token,type,properties);}
bool IOWatchdogUserClient::start(IOService *provider)
{return OSDynamicCast(IOWatchdog,provider)&&b2l_wdt_enabled()&&IOUserClient::start(provider);}
IOReturn IOWatchdogUserClient::clientClose(void)
{terminate();return kIOReturnSuccess;}
/* Exact call shapes observed in the local watchdogd, including selectors 6-8
 * absent from the older donor documentation. No unbounded output clearing. */
static IOReturn b2l_wdt_call(uint32_t selector,IOExternalMethodArguments *a)
{
    if(!a)return kIOReturnBadArgument;
    uint32_t outScalars=a->scalarOutputCount;
    uint64_t inSize=a->structureInputDescriptor?a->structureInputDescriptor->getLength():a->structureInputSize;
    uint64_t outSize=a->structureOutputDescriptor?a->structureOutputDescriptor->getLength():a->structureOutputSize;
    a->scalarOutputCount=0;a->structureOutputSize=0;a->structureOutputDescriptorSize=0;
    if(a->asyncWakePort||a->asyncReferenceCount||a->structureVariableOutputData||
       (a->structureInputDescriptor&&a->structureInput)||
       (a->structureOutputDescriptor&&a->structureOutput)||
       (a->scalarInputCount&&!a->scalarInput)||(outScalars&&!a->scalarOutput)||
       inSize>2048||outSize>2||outScalars>1)return kIOReturnBadArgument;
    if(selector>9)return kIOReturnUnsupported;
    uint32_t needIn=selector==7?1:0,needOut=(selector==0||selector==5)?1:0;
    uint32_t needStructOut=selector==9?2:0;
    if(a->scalarInputCount!=needIn||outScalars!=needOut||outSize!=needStructOut||
       (selector!=2&&inSize)||(inSize&&!a->structureInput&&!a->structureInputDescriptor))return kIOReturnBadArgument;
    if(needStructOut){
        const uint8_t zero[2]={0,0};
        if(a->structureOutputDescriptor){
            if(b2l_descriptor_write(a->structureOutputDescriptor, 0,zero,2)!=2)return kIOReturnNoSpace;
            a->structureOutputDescriptorSize=2;
        }else{
            if(!a->structureOutput)return kIOReturnBadArgument;
            memcpy(a->structureOutput,zero,2);a->structureOutputSize=2;
        }
    }
    if(needOut){a->scalarOutput[0]=0;a->scalarOutputCount=1;}
    return kIOReturnSuccess;
}
IOReturn IOWatchdogUserClient::externalMethod(uint32_t selector,IOExternalMethodArguments *a,
    IOExternalMethodDispatch *dispatch __unused,OSObject *target __unused,void *reference __unused)
{
    if(!b2l_wdt_enabled())return kIOReturnNotOpen;
    IOReturn result=b2l_wdt_call(selector,a);
    static uint32_t seen;
    if(selector<10&&!(os_atomic_or_orig(&seen,1U<<selector,relaxed)&(1U<<selector)))
        IOLog("B2L WDT: selector=%u result=0x%x\n",selector,(unsigned)result);
    return result;
}
