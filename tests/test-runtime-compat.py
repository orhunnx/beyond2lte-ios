#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Run actual ObjC adapters against native Quartz and a mocked filesystem."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[1]
common='#define B2L_COMPAT_TEST 1\n#include <assert.h>\n'
graphics=common+r'''
#import "graphics-compat.m"
static unsigned releases;
static void released(void *info,void *bytes){assert(info==(void *)42);releases++;free(bytes);}
int main(){@autoreleasepool{
 (void)b2lRuntimeMode;
 assert(rgba8(8,32,kCGImageAlphaPremultipliedFirst|kCGBitmapByteOrder32Little));
 assert(!rgba8(16,64,(CGBitmapInfo)kCGImageAlphaPremultipliedFirst));
 assert(!rgba8(8,8,(CGBitmapInfo)kCGImageAlphaOnly));
 assert(!rgba8(8,32,kCGImageAlphaPremultipliedFirst|kCGBitmapFloatComponents));
 enabled=YES;
 CGContextRef c=bitmapSimple(NULL,4,3,8,16,NULL,kCGImageAlphaPremultipliedFirst|kCGBitmapByteOrder32Little);
 assert(c&&CGColorSpaceGetModel(CGBitmapContextGetColorSpace(c))==kCGColorSpaceModelRGB);
 CGContextSetRGBFillColor(c,1,0,0,1);CGContextFillRect(c,CGRectMake(0,0,4,3));
 unsigned char *p=CGBitmapContextGetData(c);assert(p&&p[0]==0&&p[1]==0&&p[2]==255&&p[3]==255);
 CGImageRef snapshot=CGBitmapContextCreateImage(c);assert(snapshot);CGImageRelease(snapshot);CGContextRelease(c);
 CGColorSpaceRef gray=CGColorSpaceCreateDeviceGray();unsigned char values[4]={1,2,3,4};
 c=bitmapSimple(values,2,2,8,2,gray,kCGImageAlphaNone);assert(c&&CGColorSpaceGetModel(CGBitmapContextGetColorSpace(c))==kCGColorSpaceModelMonochrome);CGContextRelease(c);CGColorSpaceRelease(gray);
 // Native alpha-only contexts must retain their legal NULL space.
 unsigned before=atomic_load(&repairs);c=bitmapSimple(NULL,2,2,8,2,NULL,kCGImageAlphaOnly);assert(c&&atomic_load(&repairs)==before);CGContextRelease(c);
 CGDataProviderRef provider=CGDataProviderCreateWithData(NULL,values,sizeof(values),NULL);
 CGImageRef im=image(1,1,8,32,4,NULL,(CGBitmapInfo)kCGImageAlphaPremultipliedLast,provider,NULL,false,kCGRenderingIntentDefault);
 assert(im&&CGColorSpaceGetModel(CGImageGetColorSpace(im))==kCGColorSpaceModelRGB);CGImageRelease(im);CGDataProviderRelease(provider);
 // Release callback and allocation ownership stay native and balanced.
 c=bitmap(calloc(1,64),4,4,8,16,NULL,kCGImageAlphaPremultipliedLast,released,(void *)42);assert(c);CGContextRelease(c);assert(releases==1);
 before=atomic_load(&repairs);assert(!image(SIZE_MAX,1,8,32,4,NULL,(CGBitmapInfo)kCGImageAlphaPremultipliedLast,NULL,NULL,false,0));assert(atomic_load(&repairs)==before);
 puts("actual Quartz adapter: RGBA pixels, grayscale/mask preservation, overflow and release ownership PASS");
}}
'''
hfs=common+r'''
#import <Foundation/Foundation.h>
#import <sys/mount.h>
static int fsMode;
static int fakeStatfs(const char *path,struct statfs *fs){memset(fs,0,sizeof(*fs));strcpy(fs->f_fstypename,fsMode==1?"apfs":"hfs");fs->f_fsid.val[0]=(fsMode==2&&strcmp(path,"/"))?2:1;return 0;}
#define statfs(...) fakeStatfs(__VA_ARGS__)
#define B2L_MI_FILE_MANAGER_NAME @"B2LTestFileManager"
#import "hfs-install-compat.m"
@interface B2LTestFileManager:NSObject
+(id)defaultManager;
-(id)volumeUUIDForURL:(id)url error:(NSError **)error;
-(id)mountPointForURL:(id)url error:(NSError **)error;
@end
@implementation B2LTestFileManager
+(id)defaultManager{return [self new];}
-(id)volumeUUIDForURL:(id)url error:(NSError **)error{(void)url;(void)error;return [[NSUUID alloc] initWithUUIDString:@"00000000-0000-0000-0000-000000000123"];}
-(id)mountPointForURL:(id)url error:(NSError **)error{(void)url;(void)error;return [NSURL fileURLWithPath:@"/"];}
@end
@interface TestStager:NSObject
-(id)_stagingLocationForVolumeUUID:(id)uuid withVolumeMountPoint:(id)mount inStagingSubsytem:(NSUInteger)subsystem usingUniqueName:(BOOL)unique error:(NSError **)error;
@end
@implementation TestStager
-(id)_stagingLocationForVolumeUUID:(id)uuid withVolumeMountPoint:(id)mount inStagingSubsytem:(NSUInteger)subsystem usingUniqueName:(BOOL)unique error:(NSError **)error{
 assert([uuid isKindOfClass:NSUUID.class]&&[[(NSURL *)mount path] isEqual:@"/"]&&subsystem==1&&unique);(void)error;return @"hfs-volume";}
@end
static unsigned seenMode,calls;static BOOL *seenLink;static void *seenError;
static BOOL copied(id self,SEL sel,id src,id name,id staging,unsigned mode,uid_t uid,gid_t gid,unsigned protection,BOOL *link,NSError **error){
 (void)self;(void)sel;(void)src;(void)name;(void)staging;assert(uid==501&&gid==501&&protection==4);seenMode=mode;seenLink=link;seenError=error;if(error)*error=[NSError errorWithDomain:@"test" code:7 userInfo:nil];calls++;return YES;}
static id original(id self,SEL sel,id url,NSUInteger subsystem,BOOL unique,NSError **error){(void)self;(void)sel;(void)url;(void)error;assert(subsystem==1&&unique);calls++;return @"native";}
int main(){@autoreleasepool{
 (void)b2lRuntimeMode;assert(methodABI(class_getInstanceMethod(TestStager.class,@selector(_stagingLocationForVolumeUUID:withVolumeMountPoint:inStagingSubsytem:usingUniqueName:error:)),7,YES)==NO);
 (void)methodABI;
 nativeCopy=copied;nativeLocation=original;
 NSURL *src=[NSURL fileURLWithPath:@"/System/Library/StagedApps/Test.app"],*dst=[NSURL fileURLWithPath:@"/private/var/installd/Library/Caches/com.apple.mobile.installd.staging"];
 BOOL link=NO;NSError *error=nil;
 for(fsMode=0;fsMode<3;fsMode++)for(unsigned mode=0;mode<4;mode++){
 BOOL result=copyItem(nil,NULL,src,@"Test.app",dst,mode,501,501,4,&link,&error);
 assert(result==YES&&seenMode==((fsMode==0&&mode==2)?1:mode)&&seenLink==&link&&seenError&&error.code==7);
 }
 assert(!hfsRootURL([NSURL URLWithString:@"https://example.com"]));
 fsMode=1;assert([location(nil,NULL,src,1,YES,&error) isEqual:@"native"]);
 fsMode=2;assert([location(nil,NULL,src,1,YES,&error) isEqual:@"native"]);
 fsMode=0;assert([location([TestStager new],NULL,src,1,YES,&error) isEqual:@"hfs-volume"]);
 assert(calls==14);
 puts("actual HFS adapter: native copy mode, APFS/foreign-volume delegation and ownership/error propagation PASS");
}}
'''
with tempfile.TemporaryDirectory(prefix='b2l-runtime-test-') as td:
 p=Path(td)
 for name,code,frameworks in [('graphics',graphics,['Foundation','CoreGraphics']),('hfs',hfs,['Foundation'])]:
  (p/(name+'.m')).write_text(code)
  cmd=['clang','-fobjc-arc','-g','-O1','-Wall','-Wextra','-Werror','-Wno-deprecated-declarations','-fsanitize=address,undefined','-I',str(r/'userspace')]
  for framework in frameworks:cmd+=['-framework',framework]
  subprocess.run(cmd+[str(p/(name+'.m')),'-o',str(p/name)],check=True)
  subprocess.run([str(p/name)],check=True)
