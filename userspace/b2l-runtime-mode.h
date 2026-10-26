// SPDX-License-Identifier: BSD-2-Clause
#import <Foundation/Foundation.h>
#include <sys/sysctl.h>
#include <string.h>
static BOOL b2lToken(const char *s,const char *key) {
 size_t n=strlen(key);
 while(*s){while(*s==' ')s++;const char *e=strchr(s,' ');if(!e)e=s+strlen(s);
 if((size_t)(e-s)==n&&!memcmp(s,key,n))return YES;s=e;}return NO;
}
static BOOL b2lRuntimeMode(void) {
 char args[4096]={0},model[64]={0};size_t n=sizeof(args),m=sizeof(model);
 if(sysctlbyname("kern.bootargs",args,&n,0,0)||n>sizeof(args)||!memchr(args,0,sizeof(args))||
 sysctlbyname("hw.machine",model,&m,0,0)||m>sizeof(model)||!memchr(model,0,sizeof(model))||strcmp(model,"iPhone17,2"))return NO;
 return b2lToken(args,"b2l_userspace_bootstrap=1")&&b2lToken(args,"b2l_arm64e_compat=1")&&
 b2lToken(args,"cs_enforcement_disable=1")&&[[NSDictionary dictionaryWithContentsOfFile:@"/System/Library/CoreServices/SystemVersion.plist"][@"ProductBuildVersion"] isEqual:@"23G90"];
}
