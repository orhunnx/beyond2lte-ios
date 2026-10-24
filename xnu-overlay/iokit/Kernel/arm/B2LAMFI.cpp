/* SPDX-License-Identifier: BSD-2-Clause
 * Minimal IOKit cache-hash query for the explicitly unsigned B2L environment.
 * Only the original extensions.plist CodeDirectories have local admission pins.
 * There is no trust-cache mutation, CMS verification, or global trust fallback.
 */
#ifndef B2L_AMFI_IOKIT_HOST_TEST
#include <IOKit/IOService.h>
#include <IOKit/IOUserClient.h>
#include <IOKit/IOKitKeys.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <libkern/c++/OSBoolean.h>
#include <kern/task.h>
#include <sys/proc.h>
#include <os/atomic_private.h>
extern "C" bool b2l_amfi_active(void);
extern "C" bool b2l_amfi_is_platform(proc_t);
#endif
#include "b2l_amfi_cache_pins.h"

static bool b2l_amfi_cache_caller(task_t task)
{
    if (!b2l_amfi_active() || !task) return false;
    proc_t process = (proc_t)get_bsdtask_info(task);
    if (!process || !b2l_amfi_is_platform(process)) return false;
    OSObject *entitlement = IOUserClient::copyClientEntitlement(task,
        "com.apple.private.amfi.can-check-trust-cache");
    bool allowed = entitlement == kOSBooleanTrue;
    OSSafeReleaseNULL(entitlement);
    return allowed;
}

class AppleMobileFileIntegrity : public IOService {
    OSDeclareDefaultStructors(AppleMobileFileIntegrity);
public:
    bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
};
OSDefineMetaClassAndStructors(AppleMobileFileIntegrity, IOService);
bool AppleMobileFileIntegrity::start(IOService *provider)
{
    if (!b2l_amfi_active() || !IOService::start(provider)) return false;
    setName("AppleMobileFileIntegrity");
    if (!setProperty(kIOUserClientClassKey, "B2LAMFIUserClient") ||
        !setProperty("B2LDevelopmentPinnedExtensionCache", true)) {
        IOService::stop(provider);
        return false;
    }
    registerService();
    IOLog("B2L AMFI IO: original extension-cache pins registered; development admission only\n");
    return true;
}

class B2LAMFIUserClient : public IOUserClient {
    OSDeclareDefaultStructors(B2LAMFIUserClient);
public:
    bool initWithTask(task_t task, void *token, UInt32 type,
        OSDictionary *properties) APPLE_KEXT_OVERRIDE;
    bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
    IOReturn clientClose(void) APPLE_KEXT_OVERRIDE;
    IOReturn externalMethod(uint32_t selector, IOExternalMethodArguments *a,
        IOExternalMethodDispatch *dispatch, OSObject *target,
        void *reference) APPLE_KEXT_OVERRIDE;
private:
    bool fClosed = false;
    bool fStarted = false;
};
OSDefineMetaClassAndStructors(B2LAMFIUserClient, IOUserClient);
bool B2LAMFIUserClient::initWithTask(task_t task, void *token, UInt32 type,
    OSDictionary *properties)
{
    return type == 0 && b2l_amfi_cache_caller(task) &&
        IOUserClient::initWithTask(task, token, type, properties);
}
bool B2LAMFIUserClient::start(IOService *provider)
{
    if (!b2l_amfi_active() || !OSDynamicCast(AppleMobileFileIntegrity, provider) ||
        !IOUserClient::start(provider)) return false;
    __atomic_store_n(&fStarted, true, __ATOMIC_RELEASE);
    return true;
}
IOReturn B2LAMFIUserClient::clientClose(void)
{
    __atomic_store_n(&fClosed, true, __ATOMIC_RELEASE);
    terminate();
    return kIOReturnSuccess;
}

/* Actual iOS26 ExtensionFoundation call: selector6, scalar [1], 20-byte CDHash,
 * no outputs. Unknown hashes return the native NotFound nonmatch result. */
static IOReturn b2l_amfi_cache_call(uint32_t selector, IOExternalMethodArguments *a)
{
    if (!a) return kIOReturnBadArgument;
    uint32_t scalarOut = a->scalarOutputCount;
    uint32_t descriptorOutSize = a->structureOutputDescriptorSize;
    uint64_t inputSize = a->structureInputDescriptor ?
        a->structureInputDescriptor->getLength() : a->structureInputSize;
    uint64_t outputSize = a->structureOutputDescriptor ?
        a->structureOutputDescriptor->getLength() : a->structureOutputSize;
    a->scalarOutputCount = 0;
    a->structureOutputSize = 0;
    a->structureOutputDescriptorSize = 0;
    if (selector != 6) return kIOReturnUnsupported;
    if (a->asyncWakePort || a->asyncReferenceCount || a->structureVariableOutputData ||
        a->scalarInputCount != 1 || !a->scalarInput || a->scalarInput[0] != 1 ||
        scalarOut || outputSize || descriptorOutSize ||
        a->structureOutputDescriptor || inputSize != 20 ||
        (a->structureInputDescriptor && a->structureInputSize) ||
        (!a->structureInputDescriptor && !a->structureInput))
        return kIOReturnBadArgument;
    /* MIG supplies in-band output backing arrays even when their counts are
     * zero. Validate lengths, not those unused addresses; never write them. */
    uint8_t hash[20];
    if (a->structureInputDescriptor) {
        if (a->structureInputDescriptor->readBytes(0, hash, sizeof(hash)) != sizeof(hash))
            return kIOReturnBadArgument;
    } else {
        memcpy(hash, a->structureInput, sizeof(hash));
    }
    for (unsigned i = 0; i < sizeof(b2l_extension_cache_cdhashes) / 20; ++i)
        if (!memcmp(hash, b2l_extension_cache_cdhashes[i], sizeof(hash)))
            return kIOReturnSuccess;
    return kIOReturnNotFound;
}
IOReturn B2LAMFIUserClient::externalMethod(uint32_t selector, IOExternalMethodArguments *a,
    IOExternalMethodDispatch *dispatch __unused, OSObject *target __unused,
    void *reference __unused)
{
    if (__atomic_load_n(&fClosed, __ATOMIC_ACQUIRE) ||
        !__atomic_load_n(&fStarted, __ATOMIC_ACQUIRE) ||
        !b2l_amfi_cache_caller(current_task())) {
        if (a) { a->scalarOutputCount = 0; a->structureOutputSize = 0;
            a->structureOutputDescriptorSize = 0; }
        return kIOReturnNotPermitted;
    }
    IOReturn result = b2l_amfi_cache_call(selector, a);
    static unsigned samples;
    if (__atomic_fetch_add(&samples, 1, __ATOMIC_RELAXED) < 24)
        IOLog("B2L AMFI IO: pid=%d selector=%u result=0x%x pinned=%u\n",
            proc_selfpid(), selector, (unsigned)result, result == kIOReturnSuccess);
    return result;
}
