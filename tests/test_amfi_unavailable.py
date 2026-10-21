#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Actual entitlement-helper bodies: unavailable provider denies, never grants."""
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile


def function(text, name):
    start = text.index('\n' + name + '(') + 1
    start = text.rfind('\n', 0, start - 1) + 1
    opening = text.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end] + '\n'


def main():
    xnu = Path(sys.argv[1]).resolve()
    iokit = (xnu / 'iokit/bsddev/IOKitBSDInit.cpp').read_text()
    userclient = (xnu / 'iokit/Kernel/IOUserClient.cpp').read_text()
    kerncs = (xnu / 'bsd/kern/kern_cs.c').read_text()
    ubc = (xnu / 'bsd/kern/ubc_subr.c').read_text()
    pre = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <sys/types.h>
#define ARM64_BOARD_CONFIG_B2L 1
#define CODE_SIGNING_MONITOR 0
#define OS_ALWAYS_INLINE
#define OSENTITLEMENTS_INTERFACE_VERSION 2
#define KERN_SUCCESS 0
#define KERN_NOT_SUPPORTED 46
#define KERN_FAILURE 5
#define Z_ZERO 0
#define Z_WAITOK 0
#define Z_NOFAIL 0
#define panic(...) std::abort()
#define OSDynamicCast(type,obj) dynamic_cast<type*>(obj)
#define OSSafeReleaseNULL(obj) do{if(obj){(obj)->release();(obj)=nullptr;}}while(0)
typedef int boolean_t;
typedef int kern_return_t;
typedef unsigned zalloc_flags_t;
struct proc {};
struct vnode {};
typedef proc *proc_t;
typedef vnode *vnode_t;
typedef void *task_t;
static task_t kernel_task=(void*)1;
static unsigned queries,copies,asdicts,adjustments,info_calls,releases;
static task_t current_task(){return (void*)2;}
static void*get_bsdtask_info(task_t task){info_calls++;return task==(void*)3?nullptr:(void*)4;}
static void*kalloc_data(size_t n,zalloc_flags_t){return std::malloc(n);}
class OSObject { public: unsigned refs=1;virtual ~OSObject(){};void retain(){refs++;}void release(){releases++;assert(refs);if(!--refs)delete this;} };
class OSNumber:public OSObject { public: uint64_t unsigned64BitValue(){return 123456;} };
class OSString:public OSObject { public: size_t getLength(){return 5;}const char*getCStringNoCopy(){return "value";} };
class OSBoolean:public OSObject {};
static OSBoolean true_object,false_object;
static OSObject *kOSBooleanTrue=&true_object,*kOSBooleanFalse=&false_object;
static OSObject *result_object;
static int boolean_status=KERN_FAILURE,string_status=KERN_FAILURE,copy_status=KERN_FAILURE,adjust_status=KERN_FAILURE;
class OSDictionary:public OSObject { public:OSObject*getObject(const char*){return result_object;} };
static OSDictionary dictionary;
struct cs_blob {void *csb_entitlements;};
struct OSEntitlementsInterface {
 unsigned version;
 kern_return_t(*adjustContext)(void*,cs_blob*,const void*);
 kern_return_t(*adjustContextWithoutMonitor)(void*,cs_blob*);
 kern_return_t(*queryEntitlementBooleanWithProc)(proc_t,const char*);
 kern_return_t(*queryEntitlementStringWithProc)(proc_t,const char*,const char*);
 kern_return_t(*copyEntitlementAsOSObjectWithProc)(proc_t,const char*,void**);
};
struct amfi_type { void*(*OSEntitlements_asdict)(void*);OSEntitlementsInterface OSEntitlements; };
static amfi_type *amfi;
static kern_return_t bool_query(proc_t,const char*){queries++;return boolean_status;}
static kern_return_t string_query(proc_t,const char*,const char*){queries++;return string_status;}
static kern_return_t copy_query(proc_t,const char*,void**out){copies++;*out=nullptr;if(copy_status==KERN_SUCCESS&&result_object){result_object->retain();*out=result_object;}return copy_status;}
static void*dict_copy(void*){asdicts++;dictionary.retain();return &dictionary;}
static kern_return_t adjust(void*,cs_blob*,const void*){adjustments++;return adjust_status;}
static kern_return_t adjust_old(void*,cs_blob*){adjustments++;return adjust_status;}
static cs_blob blob={(void*)1};
static cs_blob*ubc_cs_blob_get(vnode_t vnode,int,int,off_t){return vnode?&blob:nullptr;}
class IOUserClient { public:static OSObject*copyClientEntitlement(task_t,const char*);static OSDictionary*copyClientEntitlementsVnode(vnode_t,off_t);static OSObject*copyClientEntitlementVnode(vnode_t,off_t,const char*); };
'''
    body = function(kerncs, 'csblob_entitlements_dictionary_copy')
    body += function(kerncs, 'cs_entitlements_dictionary_copy_vnode')
    body += function(userclient, 'IOUserClient::copyClientEntitlementsVnode')
    body += function(userclient, 'IOUserClient::copyClientEntitlementVnode')
    body += function(userclient, 'IOUserClient::copyClientEntitlement')
    body += function(iokit, 'copyOSStringAsCString')
    for name in ('IOTaskHasStringEntitlement', 'IOTaskHasEntitlement',
                 'IOTaskGetIntegerEntitlement', 'IOTaskGetEntitlement',
                 'IOTaskHasEntitlementAsBooleanOrObject', 'IOCurrentTaskHasStringEntitlement',
                 'IOCurrentTaskHasEntitlement', 'IOCurrentTaskGetEntitlement',
                 'IOVnodeHasEntitlement', 'IOVnodeIsEntitlementPresentWithAnyValue',
                 'IOVnodeGetBooleanEntitlement', 'IOVnodeGetIntegerEntitlement',
                 'IOVnodeGetEntitlement'):
        body += function(iokit, name)
    body += function(ubc, 'accelerate_entitlement_queries')
    post = r'''
static void denied(){
 uint64_t value=99;bool boolean=true;
 assert(!IOCurrentTaskHasStringEntitlement("test","value"));
 assert(!IOTaskHasStringEntitlement((void*)2,"test","value"));
 assert(!IOCurrentTaskHasEntitlement("test"));assert(!IOTaskHasEntitlement((void*)2,"test"));
 assert(!IOTaskGetIntegerEntitlement(nullptr,"test",&value)&&value==0);
 assert(!IOCurrentTaskGetEntitlement("test")&&!IOTaskGetEntitlement((void*)2,"test"));
 assert(!IOTaskHasEntitlementAsBooleanOrObject(nullptr,"test"));
 assert(!IOUserClient::copyClientEntitlement(nullptr,"test"));
 assert(!csblob_entitlements_dictionary_copy(&blob));
 assert(!IOVnodeHasEntitlement((vnode_t)1,0,"test"));
 assert(!IOVnodeIsEntitlementPresentWithAnyValue((vnode_t)1,0,"test"));
 assert(!IOVnodeGetBooleanEntitlement((vnode_t)1,0,"test",&boolean)&&!boolean);
 value=99;assert(!IOVnodeGetIntegerEntitlement((vnode_t)1,0,"test",&value)&&value==0);
 assert(!IOVnodeGetEntitlement((vnode_t)1,0,"test"));
 assert(accelerate_entitlement_queries(&blob)==EPERM);
}
int main(){
 amfi=nullptr;denied();assert(!queries&&!copies&&!asdicts&&!adjustments&&!info_calls);
 amfi_type provider={dict_copy,{0,adjust,adjust_old,bool_query,string_query,copy_query}};amfi=&provider;
 /* A stale table is denied even if callbacks look populated. */
 provider.OSEntitlements_asdict=nullptr;denied();assert(!queries&&!copies&&!asdicts&&!adjustments);
 provider.OSEntitlements={2,nullptr,nullptr,nullptr,nullptr,nullptr};denied();assert(!queries&&!copies&&!asdicts&&!adjustments);
 provider.OSEntitlements={2,adjust,adjust_old,bool_query,string_query,copy_query};
 boolean_status=KERN_SUCCESS;string_status=KERN_SUCCESS;copy_status=KERN_SUCCESS;adjust_status=KERN_SUCCESS;
 assert(IOCurrentTaskHasEntitlement("test")&&IOCurrentTaskHasStringEntitlement("test","value"));
 assert(IOTaskHasEntitlementAsBooleanOrObject(nullptr,"test"));
 assert(!IOTaskHasEntitlement(kernel_task,"test")&&!IOTaskHasEntitlement((void*)3,"test")&&!IOTaskHasEntitlement(nullptr,nullptr));
 /* A malformed success without an object still grants nothing. */
 result_object=nullptr;uint64_t number=99;boolean_status=KERN_FAILURE;
 assert(!IOTaskGetIntegerEntitlement(nullptr,"test",&number)&&number==0);
 assert(!IOTaskGetEntitlement(nullptr,"test")&&!IOTaskHasEntitlementAsBooleanOrObject(nullptr,"test"));
 assert(!IOUserClient::copyClientEntitlement(nullptr,"test"));
 OSNumber number_object;result_object=&number_object;assert(IOTaskGetIntegerEntitlement(nullptr,"test",&number)&&number==123456&&number_object.refs==1);
 OSString string_object;result_object=&string_object;char*string=IOTaskGetEntitlement(nullptr,"test");assert(string&&!std::strcmp(string,"value"));std::free(string);assert(string_object.refs==1);
 provider.OSEntitlements.queryEntitlementBooleanWithProc=nullptr;
 assert(!IOTaskHasEntitlement(nullptr,"test")&&IOTaskHasEntitlementAsBooleanOrObject(nullptr,"test"));
 result_object=kOSBooleanFalse;assert(!IOTaskHasEntitlementAsBooleanOrObject(nullptr,"test"));
 provider.OSEntitlements.copyEntitlementAsOSObjectWithProc=nullptr;assert(!IOTaskGetEntitlement(nullptr,"test")&&!IOTaskHasEntitlementAsBooleanOrObject(nullptr,"test"));
 provider.OSEntitlements_asdict=dict_copy;result_object=kOSBooleanTrue;bool boolean=false;assert(IOVnodeGetBooleanEntitlement((vnode_t)1,0,"test",&boolean)&&boolean);
 result_object=kOSBooleanFalse;assert(IOVnodeGetBooleanEntitlement((vnode_t)1,0,"test",&boolean)&&!boolean);
 assert(!IOVnodeGetBooleanEntitlement((vnode_t)1,0,"test",nullptr));
 number=99;assert(!IOVnodeGetIntegerEntitlement((vnode_t)1,0,"test",&number)&&number==0);
 result_object=&number_object;assert(IOVnodeGetIntegerEntitlement((vnode_t)1,0,"test",&number)&&number==123456);
 assert(!IOUserClient::copyClientEntitlementVnode(nullptr,0,"test")&&!IOUserClient::copyClientEntitlementVnode((vnode_t)1,0,nullptr));
 assert(!accelerate_entitlement_queries(&blob));provider.OSEntitlements.adjustContext=nullptr;assert(!accelerate_entitlement_queries(&blob));
 adjust_status=KERN_FAILURE;assert(accelerate_entitlement_queries(&blob)==EPERM);
 provider.OSEntitlements.adjustContextWithoutMonitor=nullptr;assert(accelerate_entitlement_queries(&blob)==EPERM);
 assert(number_object.refs==1&&string_object.refs==1&&dictionary.refs==1&&true_object.refs==1&&false_object.refs==1);
 std::puts("Actual AMFI unavailable/stale/partial/positive entitlement and acceleration tests passed");
}
'''
    with tempfile.TemporaryDirectory(prefix='b2l-amfi-test-') as td:
        path = Path(td)
        (path / 'test.cpp').write_text(pre + body + post)
        compiler = shlex.split(os.environ.get('CXX', 'clang++'))
        subprocess.run(compiler + ['-std=c++17', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                       '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                       str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
        subprocess.run([str(path / 'test')], check=True)


if __name__ == '__main__':
    main()
