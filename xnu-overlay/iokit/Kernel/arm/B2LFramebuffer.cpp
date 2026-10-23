/*
 * CPU scanout bridge for the S10+ firmware-provided framebuffer. Physical
 * range, width, height and stride come from the loader/PE console, not A5.
 * AppleCLCD matches the samsung,beyond2lte-display arm-io nub.
 *
 * iOS 26.6.2 IOMobileFramebuffer client contract inspected locally:
 *   3 default IOSurface port; 4 swap id; 5 swap record (0x588 bytes)
 *   record +0x98 swap id, +0x9c surface IDs, +0xac source rects,
 *   +0x10c destination rects, +0x17c enabled-layer mask (four layers).
 *   6 wait; 7 display id; 8 dimensions; 18 main; 28 dot pitch;
 *   72 notification control. Unsupported selectors return Unsupported.
 *
 * BGRA layers are copied without resizing. A 60 Hz software timer supplies
 * notification type 5, not hardware scanout synchronization. After the first
 * accepted copy only console drawing is suppressed; persistent logging and
 * panic drawing remain active. GPU acceleration is not provided.
 */
#include "B2LSurface.h"
#include "B2LBlit.h"

#if defined(B2L)
#include <IOKit/IOService.h>
#include <arm64/b2l_diag.h>
#include <IOKit/IOUserClient.h>
#include <IOKit/IOKitKeys.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOLocks.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/system.h>
#include <pexpert/pexpert.h>
#include <kern/clock.h>
#include <sys/proc.h>
#include <arm64/b2l_early.h>

enum {
	kB2LFBGetLayerDefaultSurface  = 3,
	kB2LFBSwapBegin               = 4,
	kB2LFBSwapEnd                 = 5,
	kB2LFBSwapWait                = 6,
	kB2LFBGetID                   = 7,
	kB2LFBGetDisplaySize          = 8,
	kB2LFBIsMainDisplay           = 18,
	kB2LFBGetDotPitch             = 28,
	kB2LFBNotifications           = 72,
};

#define B2L_FB_SWAP_REC_SIZE          0x588
#define B2L_FB_LAYERS                 4
#define B2L_FB_NOTIFY_TYPES           8
#define B2L_FB_NOTIFY_VSYNC           5
#define B2L_FB_VSYNC_US               16667
#define B2L_FB_DOT_PITCH              522     /* S10+ panel; geometry still comes from GOP */
#define B2L_FB_FORMAT_BGRA            0x42475241

class IOMobileFramebuffer : public IOService {
	OSDeclareDefaultStructors(IOMobileFramebuffer);
};

OSDefineMetaClassAndStructors(IOMobileFramebuffer, IOService);

class IOMobileFramebufferUserClient;

class AppleCLCD : public IOMobileFramebuffer {
	OSDeclareDefaultStructors(AppleCLCD);

	IOWorkLoop         *fWorkLoop;
	IOTimerEventSource *fVsync;
	uint64_t            fVsyncCount;

	static void vsyncFired(OSObject *owner, IOTimerEventSource *sender);

public:
	uint64_t      fPhys;
	uint32_t      fWidth, fHeight, fRowBytes;
	uintptr_t     fPixels;          /* kernel mapping of the panel memory */
	IOMemoryMap  *fMap;
	B2LSurface *fSurface;         /* the panel memory as an IOSurface */
	IOLock       *fLock;            /* swaps and client list */
	OSArray      *fClients;
	uint32_t      fSwapID;
	bool          fConsolePaused;

	bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
	void addClient(IOMobileFramebufferUserClient *client);
	void removeClient(IOMobileFramebufferUserClient *client);
	void swap(const uint8_t *rec);
};

OSDefineMetaClassAndStructors(AppleCLCD, IOMobileFramebuffer);

class IOMobileFramebufferUserClient : public IOUserClient {
	OSDeclareDefaultStructors(IOMobileFramebufferUserClient);

public:
	AppleCLCD          *fFB;
	task_t              fTask;
	mach_port_t         fPort;
	io_user_reference_t fFunc[B2L_FB_NOTIFY_TYPES];
	io_user_reference_t fRefcon[B2L_FB_NOTIFY_TYPES];

	bool initWithTask(task_t owningTask, void *securityID, UInt32 type) APPLE_KEXT_OVERRIDE;
	bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
	IOReturn clientClose(void) APPLE_KEXT_OVERRIDE;
	IOReturn registerNotificationPort(mach_port_t port, UInt32 type,
	    io_user_reference_t refCon) APPLE_KEXT_OVERRIDE;
	IOReturn externalMethod(uint32_t selector, IOExternalMethodArguments *args,
	    IOExternalMethodDispatch *dispatch, OSObject *target,
	    void *reference) APPLE_KEXT_OVERRIDE;
	void notify(uint32_t type, const uint64_t *values, uint32_t count);
};

OSDefineMetaClassAndStructors(IOMobileFramebufferUserClient, IOUserClient);

bool
AppleCLCD::start(IOService *provider)
{
	PE_Video video;

	if (!IOService::start(provider)) {
		return false;
	}
	PE_current_console(&video);
	uint64_t mapped_bytes=0;
    if(!b2l_early_range(&fPhys,&mapped_bytes))return false;
	fWidth = (uint32_t)video.v_width;
	fHeight = (uint32_t)video.v_height;
	fRowBytes = (uint32_t)video.v_rowBytes;
	if (fPhys == 0 || fWidth == 0 || fHeight == 0 || fWidth>8192 || fHeight>8192 || fRowBytes < fWidth * 4 || (uint64_t)fRowBytes*fHeight>mapped_bytes) {
		IOLog("B2L FB: no boot framebuffer\n");
		return false;
	}
	fLock = IOLockAlloc();
	fClients = OSArray::withCapacity(4);
	fSurface = b2l_surface_create_physical(fPhys, fWidth, fHeight, fRowBytes, B2L_FB_FORMAT_BGRA);
	fPixels = fSurface != NULL ? fSurface->kernelAddress() : 0;
	fWorkLoop = IOWorkLoop::workLoop();
	if (fLock == NULL || fClients == NULL || fPixels == 0 || fWorkLoop == NULL) {
		IOLog("B2L FB: setup failed\n");
		OSSafeReleaseNULL(fSurface);    /* stays published: also held by the table */
		OSSafeReleaseNULL(fWorkLoop);
		return false;
	}
	fVsync = IOTimerEventSource::timerEventSource(this, &AppleCLCD::vsyncFired);
	if (fVsync == NULL || fWorkLoop->addEventSource(fVsync) != kIOReturnSuccess) {
		IOLog("B2L FB: no vsync timer\n");
		return false;
	}
	fVsync->setTimeoutUS(B2L_FB_VSYNC_US);

	setProperty(kIOUserClientClassKey, "IOMobileFramebufferUserClient");
	setProperty("external", false);
	setProperty("DisplayPipePlaneBaseAlignment", 64ULL, 32);
	registerService();
	IOLog("B2L FB: AppleCLCD %ux%u stride %u at 0x%llx, default surface %u\n",
	    fWidth, fHeight, fRowBytes, fPhys, fSurface->id);
	return true;
}

void
AppleCLCD::vsyncFired(OSObject *owner, IOTimerEventSource *sender)
{
	AppleCLCD *fb = (AppleCLCD *)owner;
	uint64_t now = mach_absolute_time();
	uint64_t count = ++fb->fVsyncCount;
	uint64_t values[6] = {
		(uint32_t)count, (uint32_t)(count >> 32),
		(uint32_t)now, (uint32_t)(now >> 32),
		0, 0,
	};

	IOLockLock(fb->fLock);
	for (unsigned int i = 0; i < fb->fClients->getCount(); i++) {
		IOMobileFramebufferUserClient *client =
		    (IOMobileFramebufferUserClient *)fb->fClients->getObject(i);
		client->notify(B2L_FB_NOTIFY_VSYNC, values, 6);
	}
	IOLockUnlock(fb->fLock);
	sender->setTimeoutUS(B2L_FB_VSYNC_US);
}

void
AppleCLCD::addClient(IOMobileFramebufferUserClient *client)
{
	IOLockLock(fLock);
	fClients->setObject(client);
	IOLockUnlock(fLock);
}

void
AppleCLCD::removeClient(IOMobileFramebufferUserClient *client)
{
	IOLockLock(fLock);
	unsigned int i = fClients->getNextIndexOfObject(client, 0);
	if (i != (unsigned int)-1) {
		fClients->removeObject(i);
	}
	IOLockUnlock(fLock);
}

static uint32_t
get32(const uint8_t *p, uint32_t off)
{
	uint32_t v;
	memcpy(&v, p + off, 4);
	return v;
}

static uint32_t b2l_fb_swaps_logged;

/* Copy each enabled layer's surface into the panel memory (no scaling). */
void
AppleCLCD::swap(const uint8_t *rec)
{
	uint32_t mask = get32(rec, 0x17c);


	for (uint32_t layer = 0; layer < B2L_FB_LAYERS; layer++) {
		uint32_t id = get32(rec, 0x9c + 4 * layer);
		const uint8_t *a = rec + 0xac + 16 * layer;
		const uint8_t *b = rec + 0x10c + 16 * layer;
		int32_t sx = (int32_t)get32(a, 0), sy = (int32_t)get32(a, 4);
		int32_t dx = (int32_t)get32(b, 0), dy = (int32_t)get32(b, 4);
		int32_t w = (int32_t)get32(b, 8), h = (int32_t)get32(b, 12);

		if (!(mask & (1u << layer)) || id == 0) {
			continue;
		}
		B2LSurface *s = b2l_surface_copy(id);
		if (b2l_fb_swaps_logged < 16) {
			b2l_fb_swaps_logged++;
			IOLog("B2L FB: swap %u layer %u surface %u %ux%u fmt 0x%x bpr %u a %d,%d %ux%u b %d,%d %dx%d\n",
			    get32(rec, 0x98), layer, id, s ? s->width : 0, s ? s->height : 0,
			    s ? s->pixelFormat : 0, s ? s->bytesPerRow : 0, sx, sy, get32(a, 8),
			    get32(a, 12), dx, dy, w, h);
		}
		if (s == NULL || s == fSurface) {
			OSSafeReleaseNULL(s);
			continue;
		}
		uintptr_t src = s->kernelAddress();
        if(src && s->memory && s->bytesPerElement==4 && s->elementWidth==1 && s->elementHeight==1 &&
            s->pixelFormat==B2L_FB_FORMAT_BGRA) {
            if(w<=0||h<=0){w=(int32_t)s->width;h=(int32_t)s->height;}
            IOLockLock(fLock);
            int shown=b2l_blit((void*)fPixels,(uint64_t)fRowBytes*fHeight,fWidth,fHeight,fRowBytes,
                (const void*)src,s->memory->getLength(),s->width,s->height,s->bytesPerRow,s->offset,
                sx,sy,dx,dy,w,h);
            if(shown&&!fConsolePaused){b2l_early_gui_present();fConsolePaused=true;IOLog("B2L FB: first IOSurface frame copied to physical scanout\n");}
            __asm__ volatile("dsb sy":::"memory");
            IOLockUnlock(fLock);
        }

		s->release();
	}
}

bool
IOMobileFramebufferUserClient::initWithTask(task_t owningTask, void *securityID, UInt32 type)
{
    b2l_diag_record("B2L FB OPEN: pid=%d type=%u\n",proc_selfpid(),type);
	if (!IOUserClient::initWithTask(owningTask, securityID, type)) {
		return false;
	}
	setProperty(kIOUserClientDefaultLockingKey,kOSBooleanTrue);
	fTask = owningTask;
	return true;
}

bool
IOMobileFramebufferUserClient::start(IOService *provider)
{
	fFB = OSDynamicCast(AppleCLCD, provider);
	if (fFB == NULL || !IOUserClient::start(provider)) {
		return false;
	}
	fFB->addClient(this);
	return true;
}

IOReturn
IOMobileFramebufferUserClient::clientClose(void)
{
	if (fFB != NULL) {
		fFB->removeClient(this);
		IOLockLock(fFB->fLock);
		mach_port_t old = fPort;
		fPort = MACH_PORT_NULL;
		IOLockUnlock(fFB->fLock);
		if (old != MACH_PORT_NULL) {
			releaseNotificationPort(old);
		}
	}
	terminate();
	return kIOReturnSuccess;
}

IOReturn
IOMobileFramebufferUserClient::registerNotificationPort(mach_port_t port, __unused UInt32 type,
    __unused io_user_reference_t refCon)
{
	mach_port_t old;
	IOLockLock(fFB->fLock);
	old = fPort;
	fPort = port;
	IOLockUnlock(fFB->fLock);
	if (old != MACH_PORT_NULL) {
		releaseNotificationPort(old);
	}
	return kIOReturnSuccess;
}

/* Called with the framebuffer lock held. */
void
IOMobileFramebufferUserClient::notify(uint32_t type, const uint64_t *values, uint32_t count)
{
	OSAsyncReference64 ref;

	if (type >= B2L_FB_NOTIFY_TYPES || fFunc[type] == 0 || fPort == MACH_PORT_NULL) {
		return;
	}
	bzero(ref, sizeof(ref));
	setAsyncReference64(ref, fPort, (mach_vm_address_t)fFunc[type], fRefcon[type], fTask);
	sendAsyncResult64WithOptions(ref, kIOReturnSuccess, (io_user_reference_t *)values, count,
	    kIOUserNotifyOptionCanDrop);
}

static uint64_t b2l_fb_logged;        /* selectors already logged (< 64) */

IOReturn
IOMobileFramebufferUserClient::externalMethod(uint32_t selector, IOExternalMethodArguments *args,
    __unused IOExternalMethodDispatch *dispatch, __unused OSObject *target,
    __unused void *reference)
{
    static unsigned samples;
    if(__atomic_fetch_add(&samples,1,__ATOMIC_RELAXED)<64)
        b2l_diag_record("B2L FB CALL: pid=%d selector=%u scalar_in=%u struct_in=%llu struct_out=%llu\n",
            proc_selfpid(),selector,args?args->scalarInputCount:0,
            args?(uint64_t)args->structureInputSize:0,args?(uint64_t)args->structureOutputSize:0);

	IOReturn ret = kIOReturnSuccess;
	uint64_t out[2] = { 0, 0 };
	uint32_t outCount = 0;

	switch (selector) {
	case kB2LFBGetLayerDefaultSurface:
		out[0] = b2l_surface_port_in_task(fFB->fSurface, fTask);
		outCount = 1;
		break;
	case kB2LFBSwapBegin:
		IOLockLock(fFB->fLock);
		out[0] = ++fFB->fSwapID;
		IOLockUnlock(fFB->fLock);
		outCount = 1;
		break;
	case kB2LFBSwapEnd: {
		uint8_t rec[B2L_FB_SWAP_REC_SIZE];
		if (args->structureInputDescriptor != NULL ||
		    args->structureInput == NULL || args->structureInputSize != B2L_FB_SWAP_REC_SIZE) {
			ret = kIOReturnBadArgument;
			break;
		}
		memcpy(rec, args->structureInput, sizeof(rec));
		fFB->swap(rec);
		break;
	}
	case kB2LFBGetID:
		out[0] = 0;
		outCount = 1;
		break;
	case kB2LFBGetDisplaySize:
		out[0] = fFB->fWidth;
		out[1] = fFB->fHeight;
		outCount = 2;
		break;
	case kB2LFBIsMainDisplay:
		out[0] = 1;
		outCount = 1;
		break;
	case kB2LFBGetDotPitch:
		out[0] = B2L_FB_DOT_PITCH;
		outCount = 1;
		break;
	case kB2LFBNotifications:
		if (args->scalarInputCount >= 3 && args->scalarInput && args->scalarInput[2] < B2L_FB_NOTIFY_TYPES) {
			uint32_t type = (uint32_t)args->scalarInput[2];
			IOLockLock(fFB->fLock);
			fFunc[type] = args->scalarInput[0];
			fRefcon[type] = args->scalarInput[1];
			IOLockUnlock(fFB->fLock);
			char name[32];
			proc_selfname(name, sizeof(name));
			IOLog("B2L FB: %s notifications type %u %s\n", name, type,
			    args->scalarInput[0] ? "on" : "off");
		}
		break;
	case kB2LFBSwapWait:
		break;
    default: ret=kIOReturnUnsupported;break;
	}

    if(outCount && (args->scalarOutputCount<outCount || !args->scalarOutput))return kIOReturnNoSpace;
    for(uint32_t i=0;i<outCount;++i)args->scalarOutput[i]=out[i];
    args->scalarOutputCount=outCount;

	if (args->structureOutputDescriptor == NULL && args->structureOutput != NULL) {
		bzero(args->structureOutput, args->structureOutputSize);
	}
	if (selector >= 64 || !(b2l_fb_logged & (1ULL << selector))) {
		if (selector < 64) {
			b2l_fb_logged |= 1ULL << selector;
		}
		char name[32];
		proc_selfname(name, sizeof(name));
		IOLog("B2L FB: %s sel %u in %u/%u out %u/%u -> 0x%x\n", name, selector,
		    args->scalarInputCount, args->structureInputSize,
		    args->scalarOutputCount, args->structureOutputSize, ret);
	}
	return ret;
}
#endif /* B2L */
