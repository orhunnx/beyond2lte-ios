// SPDX-License-Identifier: BSD-2-Clause
#import "b2l-runtime-mode.h"
#import <CoreGraphics/CoreGraphics.h>
#include <stdatomic.h>
static BOOL enabled;
static _Atomic unsigned repairs;
static BOOL rgba8(size_t bits,size_t pixelBits,CGBitmapInfo info) {
 unsigned alpha=info&kCGBitmapAlphaInfoMask,order=info&kCGBitmapByteOrderMask;
 return bits==8&&pixelBits==32&&!(info&kCGBitmapFloatComponents)&&
 (alpha==kCGImageAlphaPremultipliedFirst||alpha==kCGImageAlphaPremultipliedLast||
 alpha==kCGImageAlphaFirst||alpha==kCGImageAlphaLast||alpha==kCGImageAlphaNoneSkipFirst||alpha==kCGImageAlphaNoneSkipLast)&&
 (order==kCGBitmapByteOrderDefault||order==kCGBitmapByteOrder32Little||order==kCGBitmapByteOrder32Big)&&!(info&~(kCGBitmapAlphaInfoMask|kCGBitmapByteOrderMask));
}
static CGColorSpaceRef defaultRGB(void) {
 CGColorSpaceRef space=CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
 if(!space)space=CGColorSpaceCreateDeviceRGB();return space;
}
static void repaired(const char *kind,BOOL success) {
 unsigned n=atomic_fetch_add(&repairs,1);
 if(n<16)NSLog(@"B2L CG: unprofiled RGBA8 %s sRGB result=%d",kind,success);
}
static CGImageRef image(size_t w,size_t h,size_t bits,size_t pixelBits,size_t row,CGColorSpaceRef space,CGBitmapInfo info,CGDataProviderRef provider,const CGFloat *decode,bool interpolate,CGColorRenderingIntent intent) {
 CGColorSpaceRef fallback=NULL;
 if(enabled&&!space&&w&&h&&w<=SIZE_MAX/4&&row>=w*4&&rgba8(bits,pixelBits,info))space=fallback=defaultRGB();
 CGImageRef result=CGImageCreate(w,h,bits,pixelBits,row,space,info,provider,decode,interpolate,intent);
 if(fallback){repaired("image",result!=NULL);CGColorSpaceRelease(fallback);}return result;
}
static CGContextRef bitmap(void *data,size_t w,size_t h,size_t bits,size_t row,CGColorSpaceRef space,uint32_t info,CGBitmapContextReleaseDataCallback release,void *user) {
 CGColorSpaceRef fallback=NULL;
 if(enabled&&!space&&w&&h&&w<=SIZE_MAX/4&&(!row||row>=w*4)&&rgba8(bits,32,info))space=fallback=defaultRGB();
 CGContextRef result=CGBitmapContextCreateWithData(data,w,h,bits,row,space,info,release,user);
 if(fallback){repaired("bitmap",result!=NULL);CGColorSpaceRelease(fallback);}return result;
}
static CGContextRef bitmapSimple(void *data,size_t w,size_t h,size_t bits,size_t row,CGColorSpaceRef space,uint32_t info) {
 return bitmap(data,w,h,bits,row,space,info,NULL,NULL);
}
#define INTERPOSE(replacement,original) __attribute__((used,section("__DATA,__interpose"))) static const struct {const void *newp,*oldp;} pair_##replacement={(const void *)&replacement,(const void *)&original};
#ifndef B2L_COMPAT_TEST
INTERPOSE(image,CGImageCreate)
INTERPOSE(bitmap,CGBitmapContextCreateWithData)
INTERPOSE(bitmapSimple,CGBitmapContextCreate)
__attribute__((constructor)) static void install(void) {@autoreleasepool{enabled=b2lRuntimeMode();if(enabled)NSLog(@"B2L CG: bounded unprofiled RGBA8 software compatibility loaded");}}

#endif
