/*
 * Built-in IOSurface for B2L.
 *
 * Apple's IOSurface kext is closed. IOSurface.framework (iOS 26.6.2) finds
 * IOServiceNameMatching("IOCoreSurfaceRoot"), opens it with type 0 and
 * talks to the user client below:
 *
 *   sel 13  limits: struct out 0x28, u16 at +0x22 must be 1 (ABI version),
 *           +0 offset alignment mask, +4 bytes-per-row alignment mask,
 *           +8 max bytes-per-row, +0x10 max width, +0x18 max height (u64)
 *   sel 0   create: scalar [parent id], struct in = serialized property
 *           dictionary (binary or XML), struct out = lock result (0xc68)
 *   sel 4   lookup by id: scalar [id], struct out = lock result
 *   sel 34  lookup by mach port: scalar [port name], struct out = lock result
 *   sel 35  create mach port: scalars [id, options], scalar out [port name]
 *   traps   0/1 use count +/- (id, category), 2 lock / 3 unlock
 *           (id, options, &seed), 4/5 release (id)
 *
 * The lock result tells the library where the surface lives in the
 * caller's address space: +0 pixels, +8 shared state page (registry id,
 * seed at +0xc), +0x10 timestamps, then id, sizes and geometry (layout in
 * b2l_surface_fill_result). Every surface is wired kernel memory mapped
 * into each task that creates or looks it up; mappings are counted per
 * client and dropped on release or close.
 *
 * Selectors 5, 9, 10 and 27 implement matrix, nonbulk property and bulk
 * attachment updates/readback. Unsupported notification, accelerator and
 * shared-event selectors return kIOReturnUnsupported (logged once).
 */
#include "B2LSurface.h"

#if defined(B2L)
#include <IOKit/IOService.h>
#include <arm64/b2l_diag.h>
#include <IOKit/IOUserClient.h>
#include <IOKit/IOKitKeys.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOLocks.h>
#include <IOKit/system.h>
#include <libkern/c++/OSArray.h>
#include <libkern/c++/OSDictionary.h>
#include <libkern/c++/OSNumber.h>
#include <libkern/c++/OSString.h>
#include <libkern/c++/OSSerialize.h>
#include <libkern/c++/OSUnserialize.h>
#include <sys/proc.h>

#include "B2LMemoryIO.h"

enum {
	kB2LSurfaceCreate             = 0,
	kB2LSurfaceLookup             = 4,
	kB2LSurfaceSetYCbCrMatrix     = 5,
	kB2LSurfaceSetValue           = 9,
	kB2LSurfaceCopyValues         = 10,
	kB2LSurfaceSetBulkAttachments = 27,
	kB2LSurfaceLimits             = 13,
	kB2LSurfaceLookupFromPort     = 34,
	kB2LSurfaceCreatePort         = 35,
};

enum {
	kB2LSurfaceTrapIncrementUse   = 0,
	kB2LSurfaceTrapDecrementUse   = 1,
	kB2LSurfaceTrapLock           = 2,
	kB2LSurfaceTrapUnlock         = 3,
	kB2LSurfaceTrapRelease        = 4,
	kB2LSurfaceTrapReleaseFailed  = 5,
};

#define B2L_SURFACE_RESULT_SIZE       0xc68
#define B2L_SURFACE_MAX_ID            4096
#define B2L_SURFACE_MAX_DIM           16384
#define B2L_SURFACE_MAX_ALLOC         (256u << 20)
#define B2L_SURFACE_MAX_PROPS         (64u << 10)
#define B2L_SURFACE_ROW_ALIGN         64
#define B2L_SURFACE_TIMESTAMPS        0x800   /* offset in the state page */
#define B2L_SURFACE_STATE_SEED        0xc     /* u32 in the state page */
#define kIOSurfaceLockReadOnly          0x1

static IOLock       *b2l_surface_lock;
static B2LSurface *b2l_surface_table[B2L_SURFACE_MAX_ID];
static uint32_t      b2l_surface_users[B2L_SURFACE_MAX_ID];
static uint32_t      b2l_surface_next_id = 1;
static uint32_t      b2l_surface_creates_logged;
static uint64_t      b2l_surface_wired;       /* bytes of buffer surfaces */
#define B2L_SURFACE_WIRED_MAX         (512ull << 20)

OSDefineMetaClassAndStructors(B2LSurface, OSObject);

void
B2LSurface::free()
{
	if (OSDynamicCast(IOBufferMemoryDescriptor, memory) != NULL) {
		OSAddAtomic64(-(SInt64)allocSize, (volatile SInt64 *)&b2l_surface_wired);
	}
	OSSafeReleaseNULL(properties);
	OSSafeReleaseNULL(serializedProperties);
	OSSafeReleaseNULL(kernelMap);
	OSSafeReleaseNULL(memory);
	OSSafeReleaseNULL(state);
	OSObject::free();
}

uintptr_t
B2LSurface::kernelAddress()
{
	IOBufferMemoryDescriptor *buffer = OSDynamicCast(IOBufferMemoryDescriptor, memory);
	if (buffer != NULL) {
		return (uintptr_t)buffer->getBytesNoCopy();
	}
	IOLockLock(b2l_surface_lock);
	if (kernelMap == NULL && memory != NULL) {
		kernelMap = memory->map(kIOMapWriteCombineCache);
	}
	IOLockUnlock(b2l_surface_lock);
	return kernelMap != NULL ? (uintptr_t)kernelMap->getVirtualAddress() : 0;
}

static uint32_t *
b2l_surface_seed(B2LSurface *s)
{
	return (uint32_t *)((uint8_t *)s->state->getBytesNoCopy() + B2L_SURFACE_STATE_SEED);
}

/*
 * Give the surface an id and a table slot; the table holds one retain and a
 * user count (creators, client mappings, kernel holders). Lock held.
 */
static bool
b2l_surface_insert_locked(B2LSurface *s)
{
	for (uint32_t n = 0; n < B2L_SURFACE_MAX_ID - 1; n++) {
		uint32_t id = b2l_surface_next_id;
		b2l_surface_next_id = id + 1 < B2L_SURFACE_MAX_ID ? id + 1 : 1;
		if (b2l_surface_table[id] == NULL) {
			s->id = id;
			s->retain();
			b2l_surface_table[id] = s;
			b2l_surface_users[id] = 1;
			*(uint64_t *)s->state->getBytesNoCopy() = id;   /* registry id */
			return true;
		}
	}
	return false;
}

/* Add a user to a surface that may have left the table (port lookups). */
static bool
b2l_surface_get(B2LSurface *s)
{
	bool ok = true;
	IOLockLock(b2l_surface_lock);
	if (b2l_surface_table[s->id] == s) {
		b2l_surface_users[s->id]++;
	} else if (b2l_surface_table[s->id] == NULL) {
		s->retain();
		b2l_surface_table[s->id] = s;
		b2l_surface_users[s->id] = 1;
	} else {
		ok = false;
	}
	IOLockUnlock(b2l_surface_lock);
	return ok;
}

/* Drop a user; the last one removes the surface from the table. */
static void
b2l_surface_put(B2LSurface *s)
{
	bool drop = false;
	IOLockLock(b2l_surface_lock);
	if (b2l_surface_table[s->id] == s && --b2l_surface_users[s->id] == 0) {
		b2l_surface_table[s->id] = NULL;
		drop = true;
	}
	IOLockUnlock(b2l_surface_lock);
	if (drop) {
		s->release();
	}
}

B2LSurface *
b2l_surface_copy(uint32_t id)
{
	B2LSurface *s = NULL;
	if (id == 0 || id >= B2L_SURFACE_MAX_ID || b2l_surface_lock == NULL) {
		return NULL;
	}
	IOLockLock(b2l_surface_lock);
	s = b2l_surface_table[id];
	if (s != NULL) {
		s->retain();
	}
	IOLockUnlock(b2l_surface_lock);
	return s;
}

/* The table lock; the framebuffer may publish a surface before the root starts. */
static void
b2l_surface_init_lock(void)
{
	if (b2l_surface_lock == NULL) {
		IOLock *lock = IOLockAlloc();
		if (!OSCompareAndSwapPtr(NULL, lock, &b2l_surface_lock)) {
			IOLockFree(lock);
		}
	}
}

static B2LSurface *
b2l_surface_alloc(void)
{
	b2l_surface_init_lock();
	B2LSurface *s = OSTypeAlloc(B2LSurface);
	if (s == NULL || !s->init()) {
		OSSafeReleaseNULL(s);
		return NULL;
	}
	s->state = IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task,
	    kIODirectionInOut | kIOMemoryKernelUserShared, page_size, page_size);
	if (s->state == NULL) {
		s->release();
		return NULL;
	}
	bzero(s->state->getBytesNoCopy(), page_size);
	return s;
}

static bool
b2l_surface_publish(B2LSurface *s)
{
	bool ok;
	IOLockLock(b2l_surface_lock);
	ok = b2l_surface_insert_locked(s);
	IOLockUnlock(b2l_surface_lock);
	return ok;
}

B2LSurface *
b2l_surface_create_physical(uint64_t phys, uint32_t width, uint32_t height,
    uint32_t bytesPerRow, uint32_t pixelFormat)
{
	B2LSurface *s = b2l_surface_alloc();
	if (s == NULL) {
		return NULL;
	}
	s->width = width;
	s->height = height;
	s->bytesPerRow = bytesPerRow;
	s->bytesPerElement = 4;
	s->elementWidth = s->elementHeight = 1;
	s->pixelFormat = pixelFormat;
	s->allocSize = (uint32_t)round_page(bytesPerRow * height);
	s->memory = IOMemoryDescriptor::withPhysicalAddress(phys, s->allocSize, kIODirectionInOut);
	s->mapCache = kIOMapWriteCombineCache;
	if (s->memory == NULL || !b2l_surface_publish(s)) {
		s->release();
		return NULL;
	}
	return s;
}

mach_port_name_t
b2l_surface_port_in_task(B2LSurface *s, task_t task)
{
	mach_port_name_t name = 0;
	IOUserClient::copyPortNameForObjectInTask(task, s, &name);
	return name;
}

static uint32_t
b2l_dict_u32(OSDictionary *dict, const char *key, uint32_t def)
{
	OSNumber *n = OSDynamicCast(OSNumber, dict->getObject(key));
	return n != NULL ? n->unsigned32BitValue() : def;
}

static uint32_t
b2l_div_up(uint32_t a, uint32_t b)
{
	return b > 1 ? (a + b - 1) / b : a;
}

static uint32_t
b2l_row_bytes(uint32_t width, uint32_t ew, uint32_t bpe)
{
	uint64_t row = (uint64_t)b2l_div_up(width, ew) * bpe;
	return (uint32_t)((row + B2L_SURFACE_ROW_ALIGN - 1) & ~(uint64_t)(B2L_SURFACE_ROW_ALIGN - 1));
}

/* Geometry from the creation properties (IOSurface.h keys). */
static IOReturn
b2l_surface_layout(B2LSurface *s, OSDictionary *props)
{
	uint64_t end;

	s->width = b2l_dict_u32(props, "IOSurfaceWidth", 0);
	s->height = b2l_dict_u32(props, "IOSurfaceHeight", 0);
	s->bytesPerElement = (uint16_t)b2l_dict_u32(props, "IOSurfaceBytesPerElement", 4);
	s->elementWidth = (uint8_t)b2l_dict_u32(props, "IOSurfaceElementWidth", 1);
	s->elementHeight = (uint8_t)b2l_dict_u32(props, "IOSurfaceElementHeight", 1);
	s->pixelFormat = b2l_dict_u32(props, "IOSurfacePixelFormat", 0);
	s->cacheMode = b2l_dict_u32(props, "IOSurfaceCacheMode", 0);
	s->offset = b2l_dict_u32(props, "IOSurfaceOffset", 0);
	if (s->width > B2L_SURFACE_MAX_DIM || s->height > B2L_SURFACE_MAX_DIM ||
	    s->bytesPerElement > 64 || s->elementWidth == 0 || s->elementHeight == 0) {
		return kIOReturnBadArgument;
	}
	s->bytesPerRow = b2l_dict_u32(props, "IOSurfaceBytesPerRow",
	    b2l_row_bytes(s->width, s->elementWidth, s->bytesPerElement));
	if (s->bytesPerRow < (uint64_t)b2l_div_up(s->width, s->elementWidth) * s->bytesPerElement) {
		return kIOReturnBadArgument;
	}
	end = (uint64_t)s->offset + (uint64_t)s->bytesPerRow * b2l_div_up(s->height, s->elementHeight);

	OSArray *planes = OSDynamicCast(OSArray, props->getObject("IOSurfacePlaneInfo"));
	s->planeCount = 0;
	if (planes != NULL) {
		uint64_t next = s->offset;
		if (planes->getCount() > B2L_SURFACE_MAX_PLANES) {
			return kIOReturnBadArgument;
		}
		for (unsigned int i = 0; i < planes->getCount(); i++) {
			OSDictionary *pd = OSDynamicCast(OSDictionary, planes->getObject(i));
			struct b2l_surface_plane *p = &s->planes[i];
			if (pd == NULL) {
				return kIOReturnBadArgument;
			}
			p->width = b2l_dict_u32(pd, "IOSurfacePlaneWidth", s->width);
			p->height = b2l_dict_u32(pd, "IOSurfacePlaneHeight", s->height);
			p->bytesPerElement = (uint16_t)b2l_dict_u32(pd, "IOSurfacePlaneBytesPerElement", 1);
			p->elementWidth = (uint8_t)b2l_dict_u32(pd, "IOSurfacePlaneElementWidth", 1);
			p->elementHeight = (uint8_t)b2l_dict_u32(pd, "IOSurfacePlaneElementHeight", 1);
			if (p->width > B2L_SURFACE_MAX_DIM || p->height > B2L_SURFACE_MAX_DIM ||
			    p->bytesPerElement > 64 || p->elementWidth == 0 || p->elementHeight == 0) {
				return kIOReturnBadArgument;
			}
			p->bytesPerRow = b2l_dict_u32(pd, "IOSurfacePlaneBytesPerRow",
			    b2l_row_bytes(p->width, p->elementWidth, p->bytesPerElement));
			p->offset = b2l_dict_u32(pd, "IOSurfacePlaneOffset", (uint32_t)next);
			p->size = b2l_dict_u32(pd, "IOSurfacePlaneSize",
			    p->bytesPerRow * b2l_div_up(p->height, p->elementHeight));
			next = round_page((uint64_t)p->offset + p->size);
			if ((uint64_t)p->offset + p->size > end) {
				end = (uint64_t)p->offset + p->size;
			}
		}
		s->planeCount = planes->getCount();
	}

	uint64_t alloc = b2l_dict_u32(props, "IOSurfaceAllocSize", 0);
	if (alloc < end) {
		alloc = end;
	}
	alloc = round_page(alloc);
	if (alloc == 0 || alloc > B2L_SURFACE_MAX_ALLOC) {
		return kIOReturnNoMemory;
	}
	s->allocSize = (uint32_t)alloc;
	return kIOReturnSuccess;
}

/* Per-client mapping of one surface. */
class B2LSurfaceRef : public OSObject {
	OSDeclareDefaultStructors(B2LSurfaceRef);

public:
	B2LSurface *surface;
	IOMemoryMap  *data;
	IOMemoryMap  *state;
	uint32_t      count;

	bool init() APPLE_KEXT_OVERRIDE { return OSObject::init(); }
	void free() APPLE_KEXT_OVERRIDE;
};

OSDefineMetaClassAndStructors(B2LSurfaceRef, OSObject);

void
B2LSurfaceRef::free()
{
	OSSafeReleaseNULL(data);
	OSSafeReleaseNULL(state);
	if (surface != NULL) {
		b2l_surface_put(surface);
		surface->release();
		surface = NULL;
	}
	OSObject::free();
}

class IOCoreSurfaceRoot : public IOService {
	OSDeclareDefaultStructors(IOCoreSurfaceRoot);

public:
	bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
};

OSDefineMetaClassAndStructors(IOCoreSurfaceRoot, IOService);

bool
IOCoreSurfaceRoot::start(IOService *provider)
{
	if (!IOService::start(provider)) {
		return false;
	}
	b2l_surface_init_lock();
	setProperty(kIOUserClientClassKey, "IOSurfaceRootUserClient");
	registerService();
	IOLog("B2L SURF: IOCoreSurfaceRoot registered\n");
	return true;
}

class IOSurfaceRootUserClient : public IOUserClient {
	OSDeclareDefaultStructors(IOSurfaceRootUserClient);

	task_t   fTask;
	IOLock  *fLock;
	OSArray *fRefs;

	B2LSurfaceRef *findRef(uint32_t id, unsigned int *index);
	IOReturn addRef(B2LSurface *s, B2LSurfaceRef **refOut, bool cachedLookup, bool *counted);
	IOReturn returnSurface(B2LSurface *s, IOExternalMethodArguments *args, bool cachedLookup = false);
	IOReturn create(IOExternalMethodArguments *args);
	IOReturn metadata(uint32_t selector, IOExternalMethodArguments *args);

public:
	bool initWithTask(task_t owningTask, void *securityID, UInt32 type) APPLE_KEXT_OVERRIDE;
	void free() APPLE_KEXT_OVERRIDE;
	IOReturn clientClose(void) APPLE_KEXT_OVERRIDE;
	IOReturn externalMethod(uint32_t selector, IOExternalMethodArguments *args,
	    IOExternalMethodDispatch *dispatch, OSObject *target,
	    void *reference) APPLE_KEXT_OVERRIDE;
	IOExternalTrap *getTargetAndTrapForIndex(IOService **targetP, UInt32 index) APPLE_KEXT_OVERRIDE;

	IOReturn trapUseCount(void *id, void *category, void *, void *, void *, void *);
	IOReturn trapLock(void *id, void *options, void *seedOut, void *, void *, void *);
	IOReturn trapUnlock(void *id, void *options, void *seedOut, void *, void *, void *);
	IOReturn trapRelease(void *id, void *, void *, void *, void *, void *);
};

OSDefineMetaClassAndStructors(IOSurfaceRootUserClient, IOUserClient);

static uint64_t b2l_surf_logged;      /* selectors / traps already logged (< 64) */

static void
b2l_surf_log_once(const char *what, uint32_t n)
{
	uint32_t bit = (what[0] == 't' ? 48 : 0) + n;
	if (bit >= 64 || (b2l_surf_logged & (1ULL << bit))) {
		return;
	}
	b2l_surf_logged |= 1ULL << bit;
	char name[32];
	proc_selfname(name, sizeof(name));
	IOLog("B2L SURF: %s %s %u\n", name, what, n);
}

bool
IOSurfaceRootUserClient::initWithTask(task_t owningTask, void *securityID, UInt32 type)
{
    b2l_diag_record("B2L SURF OPEN: pid=%d type=%u\n",proc_selfpid(),type);
	if (!IOUserClient::initWithTask(owningTask, securityID, type)) {
		return false;
	}
	setProperty(kIOUserClientDefaultLockingKey,kOSBooleanTrue);
	fTask = owningTask;
	fLock = IOLockAlloc();
	fRefs = OSArray::withCapacity(16);
	return fLock != NULL && fRefs != NULL;
}

void
IOSurfaceRootUserClient::free()
{
	OSSafeReleaseNULL(fRefs);
	if (fLock != NULL) {
		IOLockFree(fLock);
		fLock = NULL;
	}
	IOUserClient::free();
}

IOReturn
IOSurfaceRootUserClient::clientClose(void)
{
	IOLockLock(fLock);
	fRefs->flushCollection();
	IOLockUnlock(fLock);
	terminate();
	return kIOReturnSuccess;
}

/* Lock held. */
B2LSurfaceRef *
IOSurfaceRootUserClient::findRef(uint32_t id, unsigned int *index)
{
	for (unsigned int i = 0; i < fRefs->getCount(); i++) {
		B2LSurfaceRef *ref = (B2LSurfaceRef *)fRefs->getObject(i);
		if (ref->surface->id == id) {
			if (index != NULL) {
				*index = i;
			}
			return ref;
		}
	}
	return NULL;
}

/*
 * Count one more create/lookup of s by this client, mapping it into the
 * task the first time. Takes over the caller's user count on s.
 */
IOReturn
IOSurfaceRootUserClient::addRef(B2LSurface *s, B2LSurfaceRef **refOut, bool cachedLookup, bool *counted)
{
	*counted = false;
	IOLockLock(fLock);
	B2LSurfaceRef *ref = findRef(s->id, NULL);
	if (ref != NULL && ref->surface == s) {
		if (!cachedLookup) { ref->count++; *counted = true; }
		ref->retain(); /* protect it before dropping the client lock */
		IOLockUnlock(fLock);
		b2l_surface_put(s);
		*refOut = ref;
		return kIOReturnSuccess;
	}
	IOLockUnlock(fLock);

	ref = OSTypeAlloc(B2LSurfaceRef);
	if (ref == NULL || !ref->init()) {
		OSSafeReleaseNULL(ref);
		b2l_surface_put(s);
		return kIOReturnNoMemory;
	}
	s->retain();
	ref->surface = s;
	ref->count = 1;
	ref->data = s->memory->createMappingInTask(fTask, 0, kIOMapAnywhere | s->mapCache);
	ref->state = s->state->createMappingInTask(fTask, 0, kIOMapAnywhere | kIOMapReadOnly);
	if (ref->data == NULL || ref->state == NULL) {
		ref->release();
		return kIOReturnVMError;
	}
	IOLockLock(fLock);
	B2LSurfaceRef *raced = findRef(s->id, NULL);
	if (raced != NULL && raced->surface == s) {
		/* Another thread mapped it meanwhile: use its cached mapping. */
		if (!cachedLookup) { raced->count++; *counted = true; }
		raced->retain();
		IOLockUnlock(fLock);
		ref->release();
		*refOut = raced;
		return kIOReturnSuccess;
	}
	if (!fRefs->setObject(ref)) {
		IOLockUnlock(fLock);
		ref->release();
		return kIOReturnNoMemory;
	}
	*counted = true;
	IOLockUnlock(fLock);
	/* Transfer our retain to returnSurface; fRefs owns its own. */
	*refOut = ref;
	return kIOReturnSuccess;
}

static void
put32(uint8_t *p, uint32_t off, uint32_t v)
{
	memcpy(p + off, &v, 4);
}

static void
put64(uint8_t *p, uint32_t off, uint64_t v)
{
	memcpy(p + off, &v, 8);
}

/* IOSurface.framework's lock result (copied to IOSurfaceClient + 0x70). */
static void
b2l_surface_fill_result(B2LSurface *s, B2LSurfaceRef *ref, uint8_t *out)
{
    bzero(out, B2L_SURFACE_RESULT_SIZE);
    put64(out,0x00,ref->data->getAddress());
    put64(out,0x08,ref->state->getAddress());
    put64(out,0x10,ref->state->getAddress()+B2L_SURFACE_TIMESTAMPS);
    put32(out,0x18,s->id);
    put64(out,0x20,s->allocSize);put64(out,0x28,s->width);
    put64(out,0x30,s->height);put64(out,0x38,s->bytesPerRow);
    put64(out,0x40,s->offset);put32(out,0x48,s->pixelFormat);
    put64(out,0x50,s->allocSize);put32(out,0x58,0); /* single-plane only */
    memcpy(out+0x60,&s->bytesPerElement,2);out[0x62]=s->elementWidth;out[0x63]=s->elementHeight;
    put32(out,0x64,s->cacheMode);put32(out,0x70,s->parentID);

}

static IOReturn
b2l_struct_out(IOExternalMethodArguments *args, const void *buf, uint32_t len)
{
	if (args->structureOutputDescriptor != NULL) {
		if (len > args->structureOutputDescriptor->getLength()) {
			return kIOReturnNoSpace;
		}
		if (b2l_descriptor_write(args->structureOutputDescriptor, 0, buf, len) != len) {
			return kIOReturnVMError;
		}
		args->structureOutputDescriptorSize = len;
		return kIOReturnSuccess;
	}
	if (args->structureOutput == NULL || len > args->structureOutputSize) {
		return kIOReturnNoSpace;
	}
	memcpy(args->structureOutput, buf, len);
	args->structureOutputSize = len;
	return kIOReturnSuccess;
}

/* Map s for this client and answer with its lock result. Takes s's user count. */
IOReturn
IOSurfaceRootUserClient::returnSurface(B2LSurface *s, IOExternalMethodArguments *args, bool cachedLookup)
{
	B2LSurfaceRef *ref = NULL;
	bool counted = false;
	IOReturn ret = addRef(s, &ref, cachedLookup, &counted);
	if (ret != kIOReturnSuccess) {
		return ret;
	}
	uint8_t *out = (uint8_t *)IOMallocZeroData(B2L_SURFACE_RESULT_SIZE);
	if (out == NULL) {
		ret = kIOReturnNoMemory;
	} else {
		b2l_surface_fill_result(s, ref, out);
		/* Selector 34 tells the framework to reuse its cached client object.
		 * Its userspace retain does not require another kernel mapping count. */
		if (cachedLookup && !counted) put64(out, 0, UINT64_MAX);
		ret = b2l_struct_out(args, out, B2L_SURFACE_RESULT_SIZE);
		IOFreeData(out, B2L_SURFACE_RESULT_SIZE);
	}
	ref->release();
	if (ret != kIOReturnSuccess && counted) {
		/* Undo only the count added by this request, never a cached mapping. */
		trapRelease((void *)(uintptr_t)s->id, NULL, NULL, NULL, NULL, NULL);
	}
	return ret;
}

IOReturn
IOSurfaceRootUserClient::create(IOExternalMethodArguments *args)
{
	const char *buf = (const char *)args->structureInput;
	uint32_t len = args->structureInputSize;
	char *copy = NULL;
	IOReturn ret;

	if (args->structureInputDescriptor != NULL) {
		len = (uint32_t)args->structureInputDescriptor->getLength();
		if (len == 0 || len > B2L_SURFACE_MAX_PROPS) {
			return kIOReturnBadArgument;
		}
		copy = (char *)IOMallocData(len);
		if (copy == NULL) {
			return kIOReturnNoMemory;
		}
		if(b2l_descriptor_read(args->structureInputDescriptor, 0,copy,len)!=len){IOFreeData(copy,len);return kIOReturnNoSpace;}
		buf = copy;
	}
	OSObject *obj = (buf != NULL && len > 0 && len <= B2L_SURFACE_MAX_PROPS) ? OSUnserializeXML(buf, len) : NULL;
	if (copy != NULL) {
		IOFreeData(copy, len);
	}
	OSDictionary *props = OSDynamicCast(OSDictionary, obj);
	if (props == NULL) {
		OSSafeReleaseNULL(obj);
		IOLog("B2L SURF: create: bad property dictionary (%u bytes)\n", len);
		return kIOReturnBadArgument;
	}

	B2LSurface *s = b2l_surface_alloc();
	if (s == NULL) {
		props->release();
		return kIOReturnNoMemory;
	}
	s->parentID = args->scalarInputCount > 0 ? (uint32_t)args->scalarInput[0] : 0;
	ret = b2l_surface_layout(s, props);
    if(ret==kIOReturnSuccess && s->planeCount) ret=kIOReturnUnsupported;
	if (ret == kIOReturnSuccess) {
		IOBufferMemoryDescriptor *mem = NULL;
		if ((uint64_t)OSAddAtomic64(s->allocSize, (volatile SInt64 *)&b2l_surface_wired) +
		    s->allocSize <= B2L_SURFACE_WIRED_MAX) {
			mem = IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task,
			    kIODirectionInOut | kIOMemoryKernelUserShared, s->allocSize, page_size);
		}
		if (mem == NULL) {
			OSAddAtomic64(-(SInt64)s->allocSize, (volatile SInt64 *)&b2l_surface_wired);
			ret = kIOReturnNoMemory;
		} else {
			bzero(mem->getBytesNoCopy(), s->allocSize);
			s->memory = mem;
			s->mapCache = kIOMapDefaultCache;
			if (!b2l_surface_publish(s)) {
				ret = kIOReturnNoResources;
			}
		}
	}
	if (ret != kIOReturnSuccess || b2l_surface_creates_logged < 64) {
		char name[32];
		proc_selfname(name, sizeof(name));
		b2l_surface_creates_logged++;
		IOLog("B2L SURF: %s create id %u %ux%u fmt 0x%x bpe %u bpr %u planes %u size %u keys %u -> 0x%x\n",
		    name, s->id, s->width, s->height, s->pixelFormat, s->bytesPerElement,
		    s->bytesPerRow, s->planeCount, s->allocSize, props->getCount(), ret);
	}
	props->release();
	if (ret != kIOReturnSuccess) {
		s->release();
		return ret;
	}
	ret = returnSurface(s, args);   /* takes the creator's user count */
	s->release();
	return ret;
}

IOReturn
IOSurfaceRootUserClient::externalMethod(uint32_t selector, IOExternalMethodArguments *args,
    __unused IOExternalMethodDispatch *dispatch, __unused OSObject *target,
    __unused void *reference)
{
    static unsigned samples;
    if(__atomic_fetch_add(&samples,1,__ATOMIC_RELAXED)<64)
        b2l_diag_record("B2L SURF CALL: pid=%d selector=%u scalar_in=%u struct_in=%llu struct_out=%llu\n",
            proc_selfpid(),selector,args?args->scalarInputCount:0,
            args?(uint64_t)args->structureInputSize:0,args?(uint64_t)args->structureOutputSize:0);

	if(!args || (args->scalarInputCount && !args->scalarInput))return kIOReturnBadArgument;
    uint64_t arg0 = args->scalarInputCount > 0 ? args->scalarInput[0] : 0;

	switch (selector) {
	case kB2LSurfaceLimits: {
		if (args->scalarInputCount != 0 || args->scalarOutputCount != 0 ||
		    args->structureInputSize != 0 || args->structureInputDescriptor != NULL) {
			return kIOReturnBadArgument;
		}
		uint8_t limits[0x28] = { 0 };
		put32(limits, 0x0, B2L_SURFACE_ROW_ALIGN - 1);
		put32(limits, 0x4, B2L_SURFACE_ROW_ALIGN - 1);
		/* iOS 26 IOSurfaceClientGetPropertyMaximum reads these u64 fields.
		 * CI::SWContext caches the width as its input/output/texture limit;
		 * zero rejects every nonempty image. Keep the advertised dimensions
		 * tied to layout validation, with the existing per-allocation ceiling
		 * as the largest possible single row. Aggregate wiring is unchanged. */
		put64(limits, 0x08, B2L_SURFACE_MAX_ALLOC);
		put64(limits, 0x10, B2L_SURFACE_MAX_DIM);
		put64(limits, 0x18, B2L_SURFACE_MAX_DIM);
		limits[0x22] = 1;
		IOReturn result = b2l_struct_out(args, limits, sizeof(limits));
		static unsigned logged;
		if (result == kIOReturnSuccess &&
		    __atomic_fetch_add(&logged, 1, __ATOMIC_RELAXED) == 0) {
			b2l_diag_record("B2L SURF LIMITS: width=%u height=%u row=%u abi=1\n",
			    B2L_SURFACE_MAX_DIM, B2L_SURFACE_MAX_DIM, B2L_SURFACE_MAX_ALLOC);
		}
		return result;
	}
	case kB2LSurfaceSetYCbCrMatrix:
	case kB2LSurfaceSetValue:
	case kB2LSurfaceCopyValues:
	case kB2LSurfaceSetBulkAttachments:
		return metadata(selector, args);
	case kB2LSurfaceCreate:
		return create(args);
	case kB2LSurfaceLookup: {
		B2LSurface *s = b2l_surface_copy((uint32_t)arg0);
		IOReturn ret = kIOReturnNotFound;
		if (s != NULL) {
			ret = b2l_surface_get(s) ? returnSurface(s, args) : kIOReturnNotFound;
			s->release();
		}
		b2l_surf_log_once("sel", selector);
		return ret;
	}
	case kB2LSurfaceLookupFromPort: {
		OSObject *obj = NULL;
		IOReturn ret = IOUserClient::copyObjectForPortNameInTask(fTask, (mach_port_name_t)arg0, &obj);
		B2LSurface *s = OSDynamicCast(B2LSurface, obj);
		if (ret == kIOReturnSuccess) {
			ret = (s != NULL && b2l_surface_get(s)) ? returnSurface(s, args, true) : kIOReturnBadArgument;
		}
		OSSafeReleaseNULL(obj);
		b2l_surf_log_once("sel", selector);
		return ret;
	}
	case kB2LSurfaceCreatePort: {
		B2LSurface *s = b2l_surface_copy((uint32_t)arg0);
		if (s == NULL || args->scalarOutputCount < 1) {
			OSSafeReleaseNULL(s);
			return kIOReturnNotFound;
		}
		args->scalarOutput[0] = b2l_surface_port_in_task(s, fTask);
		args->scalarOutputCount = 1;
		s->release();
		b2l_surf_log_once("sel", selector);
		return kIOReturnSuccess;
	}
    default:
        b2l_surf_log_once("unsupported",selector);
        return kIOReturnUnsupported;
	}
}

IOReturn
IOSurfaceRootUserClient::trapUseCount(__unused void *id, __unused void *category,
    void *, void *, void *, void *)
{
	return kIOReturnSuccess;
}

static IOReturn
b2l_surface_seed_out(B2LSurface *s, void *seedOut, bool bump)
{
	uint32_t seed = bump ? OSIncrementAtomic((volatile SInt32 *)b2l_surface_seed(s)) + 1 :
	    *b2l_surface_seed(s);
	if (seedOut != NULL && copyout(&seed, (user_addr_t)seedOut, sizeof(seed)) != 0) {
		return kIOReturnBadArgument;
	}
	return kIOReturnSuccess;
}

/* Retained surface this client created or looked up, or NULL. */
static B2LSurface *
b2l_surface_held(IOLock *lock, OSArray *refs, uint32_t id)
{
	B2LSurface *s = NULL;
	IOLockLock(lock);
	for (unsigned int i = 0; i < refs->getCount(); i++) {
		B2LSurfaceRef *ref = (B2LSurfaceRef *)refs->getObject(i);
		if (ref->surface->id == id) {
			s = ref->surface;
			s->retain();
			break;
		}
	}
	IOLockUnlock(lock);
	return s;
}

/* BEGIN B2L SURFACE METADATA: shipped IOSurface.framework selectors 5/9/10/27. */
/* Bound the native deserializer and forbid references to collections: those
 * can form retain cycles. Leaf references from IOCFSerialize remain valid. */
static bool
b2l_surface_serialization_valid(const uint8_t *p, uint32_t size)
{
	if (size < 4 || size > B2L_SURFACE_MAX_PROPS) return false;
	uint32_t signature;
	memcpy(&signature, p, 4);
	if (signature != 0xd3) {
		/* Indexed binary is not emitted by the shipped IOCFSerialize(..., 1). */
		if (p[0] == 0xd4 || p[size - 1] != 0) return false;
		const char *text = (const char *)p;
		if (strnstr(text, "ID=", size) || strnstr(text, "IDREF=", size) ||
		    strnstr(text, "<!DOCTYPE", size) || strnstr(text, "<!ENTITY", size)) return false;
		uint32_t depth = 0;
		for (uint32_t i = 0; i + 1 < size; i++) {
			if (p[i] != '<' || p[i + 1] == '?' || p[i + 1] == '!') continue;
			if (p[i + 1] == '/') { if (depth == 0) return false; depth--; }
			else {
				if (++depth > 32) return false;
				uint32_t j = i + 1;
				while (j < size && p[j] != '>') j++;
				if (j == size) return false;
				/* IDs with whitespace around '=' are references too. */
				for (uint32_t k = i + 1; k + 2 < j; k++) {
					if ((p[k] == ' ' || p[k] == '\t' || p[k] == '\r' || p[k] == '\n') &&
					    p[k + 1] == 'I' && p[k + 2] == 'D') return false;
				}
				if (j > i && p[j - 1] == '/') depth--;
			}
		}
		return depth == 0;
	}
	/* One byte per object, bounded before allocation by the input size. */
	uint8_t *types = (uint8_t *)IOMallocZeroData(size / 4);
	if (types == NULL) return false;
	uint32_t count = 0, pos = 4;
	bool valid = true;
	while (pos < size && valid) {
		if (size - pos < 4 || count >= 4096) { valid = false; break; }
		uint32_t token;
		memcpy(&token, p + pos, 4); pos += 4;
		uint32_t type = (token >> 24) & 0x7f, length = token & 0xffffff;
		uint32_t bytes = 0;
		if (type == 12) { /* reference: never allow a collection target */
			if (length >= count || types[length] <= 3) valid = false;
			continue;
		}
		types[count++] = (uint8_t)type;
		switch (type) {
		case 1: case 2: case 3:
			if (length > 4096) valid = false;
			break;
		case 4:
			if (length != 8 && length != 16 && length != 32 && length != 64 &&
			    length != 31 && length != 63) valid = false;
			bytes = 8; break;
		case 8: case 9: case 10:
			bytes = (length + 3) & ~3u; break;
		case 11:
			if (length > 1) valid = false;
			break;
		default: valid = false; break;
		}
		if (bytes > size - pos) valid = false;
		else pos += bytes;
	}
	IOFreeData(types, size / 4);
	return valid && pos == size;
}

/* Read the descriptor length before narrowing; one owned snapshot is used for
 * validation and parsing, including the direct IOKit input case. */
static IOReturn
b2l_surface_metadata_input(IOExternalMethodArguments *args, uint8_t **copy,
    uint32_t *length)
{
	uint64_t n = args->structureInputDescriptor ?
	    args->structureInputDescriptor->getLength() : args->structureInputSize;
	if (n == 0 || n > B2L_SURFACE_MAX_PROPS + 12) return kIOReturnBadArgument;
	*copy = (uint8_t *)IOMallocData((size_t)n);
	if (*copy == NULL) return kIOReturnNoMemory;
	*length = (uint32_t)n;
	if (args->structureInputDescriptor) {
		if (b2l_descriptor_read(args->structureInputDescriptor, 0, *copy, n) == n) return kIOReturnSuccess;
	} else if (args->structureInput) {
		memcpy(*copy, args->structureInput, n);
		return kIOReturnSuccess;
	}
	IOFreeData(*copy, n); *copy = NULL;
	return kIOReturnBadArgument;
}

struct b2l_surface_attachment_span { uint8_t offset, length; };
static const struct b2l_surface_attachment_span b2l_surface_attachment_spans[] = {
	{0x00,32},{0x20,16},{0x30,8},
	{0x38,1},{0x39,1},{0x3a,1},{0x3b,1},{0x3c,1},{0x3d,1},{0x3e,1},{0x3f,1},
	{0x40,24},{0x58,4},{0x5c,8},{0x64,1},{0x68,8},{0x70,4},
	{0x65,1},{0x66,1},{0x74,12},{0x80,2},{0x82,2}
};

/* Caller holds the surface lock. Validate the whole update before writing. */
static IOReturn
b2l_surface_set_bulk(B2LSurface *s, const uint8_t *input, uint32_t size)
{
	if (size != 160) return kIOReturnBadArgument;
	uint64_t length, mask;
	memcpy(&length, input + 0x88, 8);
	memcpy(&mask, input + 0x90, 8);
	if (length > 132 || (mask & ~0x3fffffULL)) return kIOReturnBadArgument;
	for (unsigned int i = 0; i < 22; i++) {
		const struct b2l_surface_attachment_span *span = &b2l_surface_attachment_spans[i];
		if ((mask & (1ULL << i)) && span->offset + span->length > length)
			return kIOReturnBadArgument;
	}
	uint8_t *state = (uint8_t *)s->state->getBytesNoCopy() + 0x34;
	for (unsigned int i = 0; i < 22; i++) {
		const struct b2l_surface_attachment_span *span = &b2l_surface_attachment_spans[i];
		if (mask & (1ULL << i)) memcpy(state + span->offset, input + span->offset, span->length);
	}
	return kIOReturnSuccess;
}

/* Caller holds the surface lock. Publish only after every allocation and the
 * seed reply succeeds. A short output copy leaves dictionary and seed intact. */
static IOReturn
b2l_surface_set_values(B2LSurface *s, OSArray *array, IOExternalMethodArguments *args)
{
	uint32_t count = array->getCount();
	OSDictionary *updates = count == 1 ? OSDynamicCast(OSDictionary, array->getObject(0)) : NULL;
	OSString *key = count == 2 ? OSDynamicCast(OSString, array->getObject(1)) : NULL;
	if ((count != 1 || updates == NULL) && (count != 2 || key == NULL))
		return kIOReturnBadArgument;
	OSDictionary *next = s->properties ? OSDictionary::withDictionary(s->properties) :
	    OSDictionary::withCapacity(updates ? updates->getCount() : 1);
	if (next == NULL) return kIOReturnNoMemory;
	bool ok = updates ? next->merge(updates) : next->setObject(key, array->getObject(0));
	OSSerialize *serialized = ok ? OSSerialize::binaryWithCapacity(256) : NULL;
	IOReturn ret = kIOReturnNoMemory;
	if (serialized && next->serialize(serialized)) {
		if (serialized->getLength() > B2L_SURFACE_MAX_PROPS) ret = kIOReturnNoSpace;
		else {
			uint8_t *state = (uint8_t *)s->state->getBytesNoCopy();
			uint32_t seed;
			memcpy(&seed, state + 0x20, 4);
			seed = (seed | 1u) + 2u;
			ret = b2l_struct_out(args, &seed, sizeof(seed));
			if (ret == kIOReturnSuccess) {
				OSSafeReleaseNULL(s->properties);
				OSSafeReleaseNULL(s->serializedProperties);
				s->properties = next; next = NULL;
				s->serializedProperties = serialized; serialized = NULL;
				uint32_t bytes = s->serializedProperties->getLength();
				memcpy(state + 0x28, &bytes, 4);
				__atomic_store_n((uint32_t *)(state + 0x20), seed, __ATOMIC_RELEASE);
			}
		}
	}
	OSSafeReleaseNULL(serialized);
	OSSafeReleaseNULL(next);
	return ret;
}

static IOReturn
b2l_surface_copy_values(B2LSurface *s, IOExternalMethodArguments *args)
{
	OSSerialize *serialized = s->serializedProperties;
	if (serialized == NULL) return kIOReturnNotFound;
	uint32_t size = serialized->getLength() + 12;
	uint8_t *out = (uint8_t *)IOMallocZeroData(size);
	if (out == NULL) return kIOReturnNoMemory;
	put32(out, 4, *(uint32_t *)((uint8_t *)s->state->getBytesNoCopy() + 0x20));
	memcpy(out + 12, serialized->text(), size - 12);
	IOReturn ret = b2l_struct_out(args, out, size);
	IOFreeData(out, size);
	return ret;
}

IOReturn
IOSurfaceRootUserClient::metadata(uint32_t selector, IOExternalMethodArguments *args)
{
	uint8_t *input = NULL;
	uint32_t size = 0, id = 0;
	IOReturn ret;
	if (selector == kB2LSurfaceSetYCbCrMatrix) {
		if (args->scalarInputCount != 2 || args->scalarInput[0] > UINT32_MAX ||
		    args->scalarInput[1] > UINT8_MAX) return kIOReturnBadArgument;
		id = (uint32_t)args->scalarInput[0];
	} else {
		ret = b2l_surface_metadata_input(args, &input, &size);
		if (ret != kIOReturnSuccess) return ret;
		if ((selector == kB2LSurfaceSetBulkAttachments && size != 160) ||
		    (selector == kB2LSurfaceCopyValues && size != 13) ||
		    (selector == kB2LSurfaceSetValue && size <= 12)) {
			IOFreeData(input, size); return kIOReturnBadArgument;
		}
		memcpy(&id, input + (selector == kB2LSurfaceSetBulkAttachments ? 0x98 : 0), 4);
	}
	B2LSurface *s = b2l_surface_held(fLock, fRefs, id);
	if (s == NULL) { if (input) IOFreeData(input, size); return kIOReturnNotFound; }
	OSObject *object = NULL;
	OSArray *array = NULL;
	if (selector == kB2LSurfaceSetValue) {
		if (b2l_surface_serialization_valid(input + 12, size - 12))
			object = OSUnserializeXML((const char *)input + 12, size - 12);
		array = OSDynamicCast(OSArray, object);
		if (array == NULL) {
			OSSafeReleaseNULL(object); s->release(); IOFreeData(input, size);
			return kIOReturnBadArgument;
		}
	}
	IOLockLock(b2l_surface_lock);
	switch (selector) {
	case kB2LSurfaceSetYCbCrMatrix:
		((uint8_t *)s->state->getBytesNoCopy())[0x6e] = (uint8_t)args->scalarInput[1];
		ret = kIOReturnSuccess; break;
	case kB2LSurfaceSetBulkAttachments:
		ret = b2l_surface_set_bulk(s, input, size); break;
	case kB2LSurfaceSetValue:
		ret = b2l_surface_set_values(s, array, args); break;
	case kB2LSurfaceCopyValues:
		ret = b2l_surface_copy_values(s, args); break;
	default: ret = kIOReturnUnsupported; break;
	}
	IOLockUnlock(b2l_surface_lock);
	OSSafeReleaseNULL(object);
	s->release();
	if (input) IOFreeData(input, size);
	return ret;
}
/* END B2L SURFACE METADATA */

IOReturn
IOSurfaceRootUserClient::trapLock(void *id, __unused void *options, void *seedOut,
    void *, void *, void *)
{
	B2LSurface *s = b2l_surface_held(fLock, fRefs, (uint32_t)(uintptr_t)id);
	if (s == NULL) {
		return kIOReturnNotFound;
	}
	IOReturn ret = b2l_surface_seed_out(s, seedOut, false);
	s->release();
	return ret;
}

IOReturn
IOSurfaceRootUserClient::trapUnlock(void *id, void *options, void *seedOut,
    void *, void *, void *)
{
	B2LSurface *s = b2l_surface_held(fLock, fRefs, (uint32_t)(uintptr_t)id);
	if (s == NULL) {
		return kIOReturnNotFound;
	}
	IOReturn ret = b2l_surface_seed_out(s, seedOut,
	    !((uintptr_t)options & kIOSurfaceLockReadOnly));
	s->release();
	return ret;
}

IOReturn
IOSurfaceRootUserClient::trapRelease(void *id, void *, void *, void *, void *, void *)
{
	unsigned int index;
	IOLockLock(fLock);
	B2LSurfaceRef *ref = findRef((uint32_t)(uintptr_t)id, &index);
	if (ref != NULL && --ref->count == 0) {
		fRefs->removeObject(index);     /* unmaps and drops the user count */
	}
	IOLockUnlock(fLock);
	return ref != NULL ? kIOReturnSuccess : kIOReturnNotFound;
}

static IOExternalTrap b2l_surface_traps[] = {
	[kB2LSurfaceTrapIncrementUse]  = { NULL, (IOTrap) & IOSurfaceRootUserClient::trapUseCount },
	[kB2LSurfaceTrapDecrementUse]  = { NULL, (IOTrap) & IOSurfaceRootUserClient::trapUseCount },
	[kB2LSurfaceTrapLock]          = { NULL, (IOTrap) & IOSurfaceRootUserClient::trapLock },
	[kB2LSurfaceTrapUnlock]        = { NULL, (IOTrap) & IOSurfaceRootUserClient::trapUnlock },
	[kB2LSurfaceTrapRelease]       = { NULL, (IOTrap) & IOSurfaceRootUserClient::trapRelease },
	[kB2LSurfaceTrapReleaseFailed] = { NULL, (IOTrap) & IOSurfaceRootUserClient::trapRelease },
};

IOExternalTrap *
IOSurfaceRootUserClient::getTargetAndTrapForIndex(IOService **targetP, UInt32 index)
{
	if (index >= sizeof(b2l_surface_traps) / sizeof(b2l_surface_traps[0])) {
		b2l_surf_log_once("trap", index);
		return NULL;
	}
	b2l_surf_log_once("trap", index);
	*targetP = this;
	return &b2l_surface_traps[index];
}
#endif /* B2L */
