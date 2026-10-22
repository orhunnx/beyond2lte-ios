//
//  IOHIDDebug.h
//  IOHIDFamily
//
//  Created by YG on 10/5/15.
//
//

#ifndef IOHIDDebug_h
#define IOHIDDebug_h

#include <IOKit/IOLib.h>

/*
 * B2L: HID logging goes to the kernel console (and so the persistent
 * log) instead of os_log, so the HID path is visible in boot logs.
 */
#define HIDLog(fmt, ...)        IOLog("HID: " fmt "\n", ##__VA_ARGS__)
#define HIDLogError(fmt, ...)   IOLog("HID error: " fmt "\n", ##__VA_ARGS__)
#define HIDLogDebug(fmt, ...)   do { } while (0)
#define HIDLogInfo(fmt, ...)    do { } while (0)
#define HIDLogFault(fmt, ...)   IOLog("HID fault: " fmt "\n", ##__VA_ARGS__)

#define HIDServiceLogFault(fmt, ...)   HIDLogFault("%s:0x%llx " fmt, getName(), getRegistryEntryID(), ##__VA_ARGS__)
#define HIDServiceLogError(fmt, ...)   HIDLogError("%s:0x%llx " fmt, getName(), getRegistryEntryID(), ##__VA_ARGS__)
#define HIDServiceLog(fmt, ...)        HIDLog("%s:0x%llx " fmt, getName(), getRegistryEntryID(), ##__VA_ARGS__)
#define HIDServiceLogInfo(fmt, ...)    do { } while (0)
#define HIDServiceLogDebug(fmt, ...)   do { } while (0)

#endif /* IOHIDDebug_h */

