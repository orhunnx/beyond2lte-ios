#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Test actual trust-cache pre-dispatch guards with missing/partial providers.

Only the unchanged downstream provider path is replaced with a dispatch trap.
This verifies no unavailable call reaches Image4, AMFI, locks, DT or entitlements.
"""
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tempfile


def function(source, name, occurrence=0):
    matches=list(re.finditer(r'\n(?:static\s+)?(?:void|bool|kern_return_t)\s*\n'+name+r'\([^;]*?\)\s*\{',source))
    match=matches[occurrence]
    end,depth=match.end(),1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[match.start():end]


def main():
    source=(Path(sys.argv[1])/'bsd/kern/kern_trustcache.c').read_text()
    ready=function(source,'b2l_trust_cache_provider_complete')
    init=function(source,'trust_cache_runtime_init',2)
    init=init[:init.index('\n#endif')+len('\n#endif')]+'\n host_dispatches++;\n}\n'
    wrappers=[]
    for name in ('check_trust_cache_runtime_for_uuid','unload_trust_cache','load_trust_cache',
                 'load_trust_cache_with_type','load_legacy_trust_cache','query_trust_cache',
                 'query_trust_cache_for_rem','static_trust_cache_capabilities'):
        body=function(source,name)
        # The actual guard is the first executable block, before all original
        # input parsing, provider calls, locks and entitlement queries.
        body=body[:body.index('\n#endif')+len('\n#endif')]
        wrappers.append(body+'\n host_dispatches++;return KERN_SUCCESS;\n}\n')
    loadstatic=function(source,'load_static_trust_cache')
    loadstatic=loadstatic[:loadstatic.index('\n#endif')+len('\n#endif')]+'\n host_dispatches++;\n}\n'
    pre=r'''
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>
#define ARM64_BOARD_CONFIG_B2L 1
#ifndef __unused
#define __unused __attribute__((unused))
#endif
#define KERN_SUCCESS 0
#define KERN_INVALID_ARGUMENT 4
#define KERN_NOT_SUPPORTED 46
#define kTCTypeTotal 32
#define kTCQueryTypeTotal 8
#define kTCEntryHashSize 20
#define kUUIDSize 16
#define kTCCapabilityNone 0
#define printf(...) (void)0
#define SECURITY_READ_ONLY_LATE(t) t
typedef int kern_return_t;
typedef unsigned TCType_t,TCQueryType_t;
typedef uint64_t TCCapabilities_t;
typedef uint8_t uuid_t[16];
typedef struct {const void *trustCache,*trustCacheEntry;} TrustCacheQueryToken_t;
typedef void *DTEntry;
typedef struct {uint64_t paddr,length;} DTTrustCacheRange;
typedef struct {uint32_t count;} trust_cache_offsets_t;
static int host_dispatches;
static void *trust_cache_rt=(void*)0x1000,*trust_cache_mut_rt=(void*)0x2000;
static bool b2l_trust_cache_ready,trust_cache_static_init;
static const void *img4if;
static struct host_amfi {
 struct {
  unsigned version;
  void (*load)(void),(*loadModule)(void),(*query)(void),(*checkRuntimeForUUID)(void),(*getCapabilities)(void);
 }TrustCache;
} *amfi;
'''
    post=r'''
static void host_callback(void){host_dispatches++;}
static void denied(void){
 uint8_t hash[20]={0},uuid[16]={0},payload[8]={0},perms=0xff;
 TrustCacheQueryToken_t token;memset(&token,0xa5,sizeof(token));
 assert(query_trust_cache(0,hash,&token)==KERN_NOT_SUPPORTED);
 assert(token.trustCache==NULL&&token.trustCacheEntry==NULL);
 assert(query_trust_cache(0,hash,NULL)==KERN_NOT_SUPPORTED);
 memset(&token,0xa5,sizeof(token));assert(query_trust_cache(kTCQueryTypeTotal,hash,&token)==KERN_INVALID_ARGUMENT);
 assert(token.trustCache==NULL&&token.trustCacheEntry==NULL);
 assert(query_trust_cache(0,NULL,&token)==KERN_INVALID_ARGUMENT);
 assert(query_trust_cache_for_rem(hash,&perms)==KERN_NOT_SUPPORTED&&perms==0);
 perms=0xff;assert(query_trust_cache_for_rem(NULL,&perms)==KERN_INVALID_ARGUMENT&&perms==0);
 assert(query_trust_cache_for_rem(hash,NULL)==KERN_NOT_SUPPORTED);
 assert(check_trust_cache_runtime_for_uuid(uuid)==KERN_NOT_SUPPORTED);
 assert(check_trust_cache_runtime_for_uuid(NULL)==KERN_INVALID_ARGUMENT);
 assert(unload_trust_cache(uuid)==KERN_NOT_SUPPORTED);
 assert(unload_trust_cache(NULL)==KERN_INVALID_ARGUMENT);
 assert(load_trust_cache(payload,sizeof(payload),NULL,0)==KERN_NOT_SUPPORTED);
 assert(load_trust_cache(NULL,0,NULL,0)==KERN_INVALID_ARGUMENT);
 assert(load_trust_cache_with_type(1,payload,sizeof(payload),NULL,0,NULL,0)==KERN_NOT_SUPPORTED);
 assert(load_trust_cache_with_type(kTCTypeTotal,payload,sizeof(payload),NULL,0,NULL,0)==KERN_INVALID_ARGUMENT);
 assert(load_legacy_trust_cache(payload,sizeof(payload))==KERN_NOT_SUPPORTED);
 assert(load_legacy_trust_cache(NULL,0)==KERN_INVALID_ARGUMENT);
 /* Poisoned non-null bytes cannot be dereferenced without a provider. */
 assert(load_trust_cache_with_type(1,(void*)1,SIZE_MAX,(void*)2,SIZE_MAX,(void*)3,SIZE_MAX)==KERN_NOT_SUPPORTED);
 uint32_t count=99;TCCapabilities_t first=UINT64_MAX,second=UINT64_MAX;
 assert(static_trust_cache_capabilities(&count,&first,&second)==KERN_NOT_SUPPORTED);
 assert(!count&&!first&&!second);
 first=second=UINT64_MAX;assert(static_trust_cache_capabilities(NULL,&first,&second)==KERN_INVALID_ARGUMENT);
 assert(!first&&!second);
 assert(static_trust_cache_capabilities(NULL,NULL,NULL)==KERN_INVALID_ARGUMENT);
 load_static_trust_cache();assert(trust_cache_static_init);
 assert(host_dispatches==0);
}
int main(void){
 denied(); /* Safe even before the runtime initializer has run. */
 trust_cache_runtime_init();assert(!b2l_trust_cache_ready&&!trust_cache_rt&&!trust_cache_mut_rt);denied();
 struct host_amfi provider={.TrustCache={.version=2,.load=host_callback,.loadModule=host_callback,
  .query=host_callback,.checkRuntimeForUUID=host_callback,.getCapabilities=host_callback}};
 amfi=&provider;assert(!b2l_trust_cache_provider_complete());
 img4if=(void*)1;assert(b2l_trust_cache_provider_complete());
 trust_cache_runtime_init();assert(host_dispatches==1);host_dispatches=0; /* Complete providers pass the presence gate. */
 provider.TrustCache.version=1;assert(!b2l_trust_cache_provider_complete());
 provider.TrustCache.version=2;
 void (**callbacks[])(void)={&provider.TrustCache.load,&provider.TrustCache.loadModule,
  &provider.TrustCache.query,&provider.TrustCache.checkRuntimeForUUID,&provider.TrustCache.getCapabilities};
 for(unsigned i=0;i<5;++i){*callbacks[i]=NULL;assert(!b2l_trust_cache_provider_complete());
  trust_cache_runtime_init();assert(!b2l_trust_cache_ready);denied();*callbacks[i]=host_callback;}
 amfi=NULL;assert(!b2l_trust_cache_provider_complete());denied();
 assert(host_dispatches==0);
 puts("B2L trust-cache pre-init/missing/stale/partial provider guards: ASan/UBSan passed");
}
'''
    with tempfile.TemporaryDirectory(prefix='b2l-trustcache-') as temporary:
        root=Path(temporary);src=root/'test.c';binary=root/'test'
        src.write_text(pre+ready+init+'\n'.join(wrappers)+loadstatic+post)
        args=shlex.split(os.environ.get('CC','clang'))
        subprocess.run(args+['-std=c11','-g','-O1','-fsanitize=address,undefined',str(src),'-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True)
    assert 'static SECURITY_READ_ONLY_LATE(bool) b2l_trust_cache_ready;' in source
    print('All eight public APIs and static loading stop before unavailable provider/DT/entitlement dispatch')


if __name__=='__main__':
    main()
