/*
 * B2L: minimal IOHIDEventService.
 *
 * Apple's IOHIDFamily kext is not part of xnu. The iOS 16.7 userland
 * (IOKit.framework's HID event system, IOHIDFamily-1915.140.3) talks to
 * IOHIDEventService objects through IOHIDEventServiceUserClient, which is
 * compiled here unchanged from IOHIDFamily-1915.140.3 together with
 * IOHIDEventServiceQueue and IOHIDEvent (the shared-memory event queue and
 * the event serialization). This class provides only the part of the real
 * IOHIDEventService interface that the user client uses, plus dispatchEvent
 * for drivers. It does not parse HID report descriptors: subclasses build
 * IOHIDEvents themselves.
 */
#ifndef _IOKIT_HID_IOHIDEVENTSERVICE_H
#define _IOKIT_HID_IOHIDEVENTSERVICE_H

#include <IOKit/IOService.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOCommandGate.h>
#include "IOHIDEventTypes.h"

class IOHIDEvent;

/* dispatchEvent options (IOHIDFamily IOHIDEventService.h) */
enum {
	kHIDDispatchOptionDeliveryNotificationForce         = (1 << 12),
	kHIDDispatchOptionDeliveryNotificationSuppress      = (1 << 13)
};

class IOHIDEventService : public IOService
{
	OSDeclareDefaultStructors(IOHIDEventService);

public:
	typedef void (*Action)(OSObject *target, OSObject *sender, void *context, OSObject *event, IOOptionBits options);

	virtual bool            start(IOService *provider) APPLE_KEXT_OVERRIDE;
    IOWorkLoop *getWorkLoop() const APPLE_KEXT_OVERRIDE {return _hidWorkLoop;}
	virtual void            free(void) APPLE_KEXT_OVERRIDE;

	virtual bool            open(IOService *client, IOOptionBits options, void *context, Action action);
	virtual void            close(IOService *forClient, IOOptionBits options = 0) APPLE_KEXT_OVERRIDE;

	virtual IOHIDEvent *    copyEvent(IOHIDEventType type, IOHIDEvent *matching = 0, IOOptionBits options = 0);
	virtual IOHIDEvent *    copyMatchingEvent(OSDictionary *matching);
	virtual IOReturn        setElementValue(UInt32 usagePage, UInt32 usage, UInt32 value);
	virtual IOReturn        setProperties(OSObject *properties) APPLE_KEXT_OVERRIDE;

	virtual UInt32          getPrimaryUsagePage();
	virtual UInt32          getPrimaryUsage();

	virtual IOReturn        newUserClient(task_t owningTask, void *securityID, UInt32 type,
	    OSDictionary *properties, IOUserClient **handler) APPLE_KEXT_OVERRIDE;

protected:
	virtual bool            handleOpen(IOService *client, IOOptionBits options, void *argument) APPLE_KEXT_OVERRIDE;
	virtual void            handleClose(IOService *client, IOOptionBits options) APPLE_KEXT_OVERRIDE;
	virtual bool            handleIsOpen(const IOService *client) const APPLE_KEXT_OVERRIDE;

	bool                    handleIsOpenLocked(const IOService *client) const;
	virtual void            dispatchEvent(IOHIDEvent *event, IOOptionBits options = 0);

	/* Published as HID properties by start(). */
	virtual OSString *      getTransport();
	virtual OSString *      getProduct();
	virtual UInt32          getVendorID();
	virtual UInt32          getProductID();
	virtual UInt32          getReportInterval();

	IOWorkLoop *            _hidWorkLoop;
	IOCommandGate *         _hidGate;
	IOLock *                _clientLock;
	OSArray *               _clients;	/* IOHIDEventServiceClient per open client */
};

#endif /* _IOKIT_HID_IOHIDEVENTSERVICE_H */
