// SPDX-License-Identifier: BSD-2-Clause
// HFS staging uses real volume metadata and the original installer operations.
#import "b2l-runtime-mode.h"
#import <objc/runtime.h>
#import <objc/message.h>
#include <sys/mount.h>
#ifndef B2L_MI_FILE_MANAGER_NAME
#define B2L_MI_FILE_MANAGER_NAME @"MIFileManager"
#endif
static id (*nativeLocation)(id,SEL,id,NSUInteger,BOOL,NSError **);
static BOOL (*nativeCopy)(id,SEL,id,id,id,unsigned,uid_t,gid_t,unsigned,BOOL *,NSError **);
static BOOL hfsRootURL(NSURL *url) {
 if(![url isKindOfClass:NSURL.class]||!url.isFileURL)return NO;
 NSString *path=url.path;struct statfs fs,root;
 while(path.length>1&&statfs(path.fileSystemRepresentation,&fs))path=path.stringByDeletingLastPathComponent;
 return !statfs(path.fileSystemRepresentation,&fs)&&!statfs("/",&root)&&!strcmp(fs.f_fstypename,"hfs")&&
 !memcmp(&fs.f_fsid,&root.f_fsid,sizeof(fs.f_fsid));
}
static id location(id self,SEL cmd,id url,NSUInteger subsystem,BOOL unique,NSError **error) {
 if(!hfsRootURL(url))return nativeLocation(self,cmd,url,subsystem,unique,error);
 id fm=((id(*)(id,SEL))objc_msgSend)(NSClassFromString(B2L_MI_FILE_MANAGER_NAME),NSSelectorFromString(@"defaultManager"));
 SEL volume=NSSelectorFromString(@"volumeUUIDForURL:error:"),mount=NSSelectorFromString(@"mountPointForURL:error:");
 SEL next=NSSelectorFromString(@"_stagingLocationForVolumeUUID:withVolumeMountPoint:inStagingSubsytem:usingUniqueName:error:");
 if(![fm respondsToSelector:volume]||![fm respondsToSelector:mount]||![self respondsToSelector:next])return nativeLocation(self,cmd,url,subsystem,unique,error);
 NSError *failure=nil;
 id uuid=((id(*)(id,SEL,id,NSError **))objc_msgSend)(fm,volume,url,&failure);
 id point=uuid?((id(*)(id,SEL,id,NSError **))objc_msgSend)(fm,mount,url,&failure):nil;
 if(![uuid isKindOfClass:NSUUID.class]||![point isKindOfClass:NSURL.class]){if(error)*error=failure;return nil;}
 id result=((id(*)(id,SEL,id,id,NSUInteger,BOOL,NSError **))objc_msgSend)(self,next,uuid,point,subsystem,unique,error);
 NSLog(@"B2L HFS: native volume staging subsystem=%lu unique=%d result=%d",(unsigned long)subsystem,unique,result!=nil);
 return result;
}
static BOOL copyItem(id self,SEL cmd,id source,id name,id staging,unsigned mode,uid_t uid,gid_t gid,unsigned protection,BOOL *symlink,NSError **error) {
 // Mode 2 uses clonefile with only EXDEV fallback. Mode 1 uses native
 // copyfile with the same traversal, ownership, protection and symlink checks.
 if(mode==2&&hfsRootURL(source)&&hfsRootURL(staging)){
  mode=1;NSLog(@"B2L HFS: clone staging selected native copyfile");
 }
 return nativeCopy(self,cmd,source,name,staging,mode,uid,gid,protection,symlink,error);
}
static BOOL methodABI(Method method,NSUInteger count,BOOL objectResult) {
 if(!method)return NO;NSMethodSignature *s=[NSMethodSignature signatureWithObjCTypes:method_getTypeEncoding(method)];
 if(s.numberOfArguments!=count)return NO;
 const char *kinds=objectResult?"@QB^":"@@@IIII^^";
 for(NSUInteger i=2;i<count;i++){
  const char *type=[s getArgumentTypeAtIndex:i];while(*type&&strchr("rnNoORV",*type))type++;
  char expected=kinds[i-2];
  if(expected=='B'){if(*type!='B'&&*type!='c')return NO;}
  else if(*type!=expected)return NO;
 }
 if(objectResult)return s.methodReturnType[0]=='@';
 return s.methodReturnLength==sizeof(BOOL)&&(s.methodReturnType[0]=='B'||s.methodReturnType[0]=='c');
}
#ifndef B2L_COMPAT_TEST
__attribute__((constructor)) static void install(void) {
 @autoreleasepool {
 if(!b2lRuntimeMode())return;
 Class manager=NSClassFromString(@"MIStagingManager"),fm=NSClassFromString(B2L_MI_FILE_MANAGER_NAME);
 Method a=class_getInstanceMethod(manager,NSSelectorFromString(@"stagingLocationForURL:withinStagingSubsytem:usingUniqueName:error:"));
 Method b=class_getInstanceMethod(fm,NSSelectorFromString(@"_stageURLByCopying:toItemName:inStagingDir:stagingMode:settingUID:gid:dataProtectionClass:hasSymlink:error:"));
 if(methodABI(a,6,YES))nativeLocation=(void *)method_setImplementation(a,(IMP)location);
 if(methodABI(b,11,NO))nativeCopy=(void *)method_setImplementation(b,(IMP)copyItem);
 NSLog(@"B2L HFS: compatibility loaded location=%d copy=%d",nativeLocation!=NULL,nativeCopy!=NULL);
 }
}

#endif
