/*
 * B2L: the two IOGraphics types the HID headers use (IOGraphicsFamily
 * is not part of xnu). Layouts as in IOKit/graphics/IOGraphicsTypes.h.
 */
#ifndef _IOKIT_IOGRAPHICSTYPES_H
#define _IOKIT_IOGRAPHICSTYPES_H

#include <IOKit/IOTypes.h>

typedef struct IOGPoint {
	SInt16 x;
	SInt16 y;
} IOGPoint;

typedef struct IOGBounds {
	SInt16 minx;
	SInt16 maxx;
	SInt16 miny;
	SInt16 maxy;
} IOGBounds;

#endif /* _IOKIT_IOGRAPHICSTYPES_H */
