#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Exercise actual Mach provider code with native callback ownership contracts."""
import os, pathlib, shutil, subprocess, tempfile
root = pathlib.Path(__file__).resolve().parents[1]
provider = root / "xnu-overlay/bsd/kern/b2l_mach_filter.h"
prefix = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#ifndef __unused
#define __unused __attribute__((unused))
#endif
#define FALSE 0
#define KERN_SUCCESS 0
#define KERN_INVALID_ARGUMENT 4
#define KERN_RESOURCE_SHORTAGE 6
#define KERN_NOT_SUPPORTED 46
#define KERN_FAILURE 5
#define MPO_PORT_TYPE_MASK 0x1dc00u
#define MPO_CONNECTION_PORT 0x800u
#define MPO_IMMOVABLE_RECEIVE 0x80u
#define MPO_FILTER_MSG 0x100u
#define MPO_ANONYMOUS_SERVICE UINT32_C(0xfffffffe)
#define MACH_MSG_FILTER_POLICY_ALLOW 0
#define MACH_MSG_FILTER_CALLBACKS_VERSION_1 1
#define Z_WAITOK 1
#define Z_ZERO 2
struct task;
typedef int boolean_t, kern_return_t, mach_msg_id_t, mach_msg_filter_id;
struct mach_service_port_info { char name[255]; uint8_t domain; };
typedef struct mach_service_port_info *mach_service_port_info_t;
typedef struct { _Atomic unsigned count; } os_refcnt_t;
static _Atomic unsigned allocations, frees;
static bool enabled = true, fail_alloc;
static bool b2l_sandbox_development_enabled(void) { return enabled; }
static void os_ref_init(os_refcnt_t *p, void *group) { (void)group; atomic_init(&p->count, 1); }
static void os_ref_retain(os_refcnt_t *p) { unsigned n=atomic_fetch_add(&p->count,1); assert(n && n<UINT32_MAX); }
static unsigned os_ref_release(os_refcnt_t *p) { unsigned n=atomic_fetch_sub(&p->count,1); assert(n); return n-1; }
static void *allocate(size_t n) { if(fail_alloc)return NULL;void *p=calloc(1,n);assert(p);atomic_fetch_add(&allocations,1);return p; }
static void deallocate(void *p) { atomic_fetch_add(&frees,1);free(p); }
#define kalloc_type(type, flags) allocate(sizeof(type))
#define kfree_type(type, pointer) deallocate(pointer)
struct mach_msg_filter_callbacks {
 unsigned version;
 boolean_t (*fetch_filter_policy)(struct task *,void *,mach_msg_id_t,mach_msg_filter_id *);
 kern_return_t (*alloc_service_port_sblabel)(mach_service_port_info_t,void **);
 void (*dealloc_service_port_sblabel)(void *);
 void *(*derive_sblabel_from_service_port)(void *,boolean_t *);
 kern_return_t (*get_connection_port_filter_policy)(void *,void *,uint64_t *);
 void (*retain_sblabel)(void *);
};
static const struct mach_msg_filter_callbacks *registered;
static unsigned registration_calls;
static kern_return_t mach_msg_filter_register_callback(const struct mach_msg_filter_callbacks *c) {
 registration_calls++;if(registered)return KERN_FAILURE;
 assert(c->version==1&&c->fetch_filter_policy&&c->alloc_service_port_sblabel&&
 c->dealloc_service_port_sblabel&&c->derive_sblabel_from_service_port&&
 c->get_connection_port_filter_policy&&c->retain_sblabel);registered=c;return 0;
}
"""
suffix = r"""
static struct mach_service_port_info info;
static void *make_label(void) { void *p=NULL;assert(!b2l_mach_filter_alloc(&info,&p)&&p);return p; }
static unsigned refs(void *p) { return atomic_load(&((struct b2l_mach_service_label *)p)->references.count); }
static void retain_pair(void *a,void *b) { b2l_mach_filter_retain(a);b2l_mach_filter_retain(b); }
static void test_registration(void) {
 enabled=false;assert(b2l_mach_filter_register()==KERN_NOT_SUPPORTED&&!registration_calls);
 enabled=true;static const struct mach_msg_filter_callbacks foreign={0};registered=&foreign;
 assert(b2l_mach_filter_register()==KERN_FAILURE&&!b2l_mach_filter_owned);void *inactive=(void*)1;
 assert(!b2l_mach_filter_anonymous_connection(MPO_CONNECTION_PORT,MPO_ANONYMOUS_SERVICE,&inactive)&&!inactive);
 registered=NULL;assert(!b2l_mach_filter_register()&&registered==&b2l_mach_filter_callbacks&&b2l_mach_filter_owned);
 assert(b2l_mach_filter_register()==KERN_FAILURE&&registered==&b2l_mach_filter_callbacks&&b2l_mach_filter_owned);
}
static void test_allocation_errors(void) {
 void *p=(void*)1;assert(b2l_mach_filter_alloc(NULL,&p)==KERN_INVALID_ARGUMENT&&!p);
 assert(b2l_mach_filter_alloc(&info,NULL)==KERN_INVALID_ARGUMENT);
 enabled=false;p=(void*)1;assert(b2l_mach_filter_alloc(&info,&p)==KERN_NOT_SUPPORTED&&!p);
 enabled=true;fail_alloc=true;assert(b2l_mach_filter_alloc(&info,&p)==KERN_RESOURCE_SHORTAGE&&!p);fail_alloc=false;
}
static void test_relation_and_ownership(void) {
 void *a=make_label(),*b=make_label();boolean_t filtered=1;uint64_t policy=999;
 b2l_mach_filter_retain(a);void *connection=b2l_mach_filter_derive(a,&filtered);
 assert(connection==a&&!filtered&&refs(a)==2);
 retain_pair(a,connection);assert(refs(a)==4);
 assert(!b2l_mach_filter_connection(a,connection,&policy)&&!policy&&refs(a)==2);
 retain_pair(a,b);policy=999;assert(b2l_mach_filter_connection(a,b,&policy)==KERN_INVALID_ARGUMENT&&!policy);
 assert(refs(a)==2&&refs(b)==1);
 b2l_mach_filter_retain(a);assert(b2l_mach_filter_connection(a,NULL,&policy)==KERN_INVALID_ARGUMENT&&refs(a)==2);
 b2l_mach_filter_retain(b);assert(b2l_mach_filter_connection(NULL,b,&policy)==KERN_INVALID_ARGUMENT&&refs(b)==1);
 assert(b2l_mach_filter_connection(NULL,NULL,&policy)==KERN_INVALID_ARGUMENT&&!policy);
 retain_pair(a,a);assert(b2l_mach_filter_connection(a,a,NULL)==KERN_INVALID_ARGUMENT&&refs(a)==2);
 b2l_mach_filter_release(connection);assert(refs(a)==1);
 /* A connection retains its parent identity after native service teardown. */
 b2l_mach_filter_retain(a);connection=b2l_mach_filter_derive(a,&filtered);
 b2l_mach_filter_release(a);assert(refs(connection)==1);
 b2l_mach_filter_release(connection);b2l_mach_filter_release(b);
 b2l_mach_filter_retain(NULL);b2l_mach_filter_release(NULL);
}
static void test_anonymous_forward(void) {
 void *a=make_label(),*b=make_label(),*anon=NULL;uint64_t policy=999;boolean_t filtered=1;
 assert(!b2l_mach_filter_anonymous_connection(0x283f,MPO_ANONYMOUS_SERVICE,&anon)&&anon);
 assert(((struct b2l_mach_service_label*)anon)->kind==B2L_MACH_ANONYMOUS_FORWARD&&refs(anon)==1);
 retain_pair(a,anon);assert(!b2l_mach_filter_connection(a,anon,&policy)&&!policy&&refs(a)==1&&refs(anon)==1);
 retain_pair(b,anon);assert(!b2l_mach_filter_connection(b,anon,&policy)&&refs(b)==1&&refs(anon)==1);
 retain_pair(anon,anon);assert(b2l_mach_filter_connection(anon,anon,&policy)==KERN_INVALID_ARGUMENT&&refs(anon)==1);
 b2l_mach_filter_retain(anon);assert(!b2l_mach_filter_derive(anon,&filtered)&&!filtered&&refs(anon)==1);
 void *out=(void*)1;assert(b2l_mach_filter_anonymous_connection(MPO_CONNECTION_PORT|MPO_IMMOVABLE_RECEIVE,MPO_ANONYMOUS_SERVICE,&out)==KERN_INVALID_ARGUMENT&&!out);
 assert(b2l_mach_filter_anonymous_connection(0,MPO_ANONYMOUS_SERVICE,&out)==KERN_INVALID_ARGUMENT&&!out);
 assert(b2l_mach_filter_anonymous_connection(MPO_CONNECTION_PORT,UINT32_MAX,&out)==KERN_INVALID_ARGUMENT&&!out);
 assert(b2l_mach_filter_anonymous_connection(MPO_CONNECTION_PORT,123,&out)==KERN_INVALID_ARGUMENT&&!out);
 assert(b2l_mach_filter_anonymous_connection(MPO_CONNECTION_PORT,MPO_ANONYMOUS_SERVICE,NULL)==KERN_INVALID_ARGUMENT);
 fail_alloc=true;assert(b2l_mach_filter_anonymous_connection(MPO_CONNECTION_PORT,MPO_ANONYMOUS_SERVICE,&out)==KERN_RESOURCE_SHORTAGE&&!out);fail_alloc=false;
 enabled=false;assert(!b2l_mach_filter_anonymous_connection(MPO_CONNECTION_PORT,MPO_ANONYMOUS_SERVICE,&out)&&!out);
 retain_pair(a,anon);assert(b2l_mach_filter_connection(a,anon,&policy)==KERN_NOT_SUPPORTED&&refs(anon)==1&&refs(a)==1);enabled=true;
 b2l_mach_filter_release(a);b2l_mach_filter_release(b);b2l_mach_filter_release(anon);
}
static void test_runtime_gate(void) {
 void *a=make_label();boolean_t filtered=1;uint64_t policy=999;mach_msg_filter_id fid=999;
 enabled=false;b2l_mach_filter_retain(a);assert(!b2l_mach_filter_derive(a,&filtered)&&!filtered&&refs(a)==1);
 retain_pair(a,a);assert(b2l_mach_filter_connection(a,a,&policy)==KERN_NOT_SUPPORTED&&!policy&&refs(a)==1);
 b2l_mach_filter_retain(a);assert(!b2l_mach_filter_fetch(NULL,a,123,&fid)&&!fid&&refs(a)==1);
 enabled=true;b2l_mach_filter_retain(a);assert(b2l_mach_filter_fetch(NULL,a,123,&fid)&&!fid&&refs(a)==1);
 b2l_mach_filter_retain(a);assert(!b2l_mach_filter_fetch(NULL,a,123,NULL)&&refs(a)==1);
 assert(!b2l_mach_filter_fetch(NULL,NULL,123,&fid)&&!fid);
 b2l_mach_filter_retain(a);assert(!b2l_mach_filter_derive(a,NULL)&&refs(a)==1);
 assert(!b2l_mach_filter_derive(NULL,&filtered)&&!filtered);b2l_mach_filter_release(a);
}
static void *concurrent(void *a) {
 for(unsigned i=0;i<10000;i++) {
  boolean_t filtered=1;uint64_t policy=99;mach_msg_filter_id fid=99;
  b2l_mach_filter_retain(a);void *connection=b2l_mach_filter_derive(a,&filtered);assert(connection==a&&!filtered);
  retain_pair(a,connection);assert(!b2l_mach_filter_connection(a,connection,&policy)&&!policy);
  b2l_mach_filter_retain(connection);assert(b2l_mach_filter_fetch(NULL,connection,0x77303074,&fid)&&!fid);
  b2l_mach_filter_release(connection);
 } return NULL;
}
static void *concurrent_anonymous(void *arg) {
 void **pair=arg;for(unsigned i=0;i<10000;i++){
  uint64_t policy=99;mach_msg_filter_id filter=99;
  retain_pair(pair[0],pair[1]);assert(!b2l_mach_filter_connection(pair[0],pair[1],&policy)&&!policy);
  b2l_mach_filter_retain(pair[1]);assert(b2l_mach_filter_fetch(NULL,pair[1],0,&filter)&&!filter);
 }return NULL;
}
static void test_concurrent_lifetime(void) {
 void *a=make_label();pthread_t threads[8];
 for(unsigned i=0;i<8;i++)assert(!pthread_create(&threads[i],NULL,concurrent,a));
 for(unsigned i=0;i<8;i++)assert(!pthread_join(threads[i],NULL));
 assert(refs(a)==1);b2l_mach_filter_release(a);
 a=make_label();void *anon=NULL;assert(!b2l_mach_filter_anonymous_connection(MPO_CONNECTION_PORT,MPO_ANONYMOUS_SERVICE,&anon));void *pair[]={a,anon};
 for(unsigned i=0;i<8;i++)assert(!pthread_create(&threads[i],NULL,concurrent_anonymous,pair));
 for(unsigned i=0;i<8;i++)assert(!pthread_join(threads[i],NULL));
 assert(refs(a)==1&&refs(anon)==1);b2l_mach_filter_release(a);b2l_mach_filter_release(anon);
}
int main(void) {
 test_registration();test_allocation_errors();test_relation_and_ownership();test_anonymous_forward();test_runtime_gate();test_concurrent_lifetime();
 assert(atomic_load(&allocations)==atomic_load(&frees));
 puts("Mach filter v1: actual provider, owned anonymous forward kind, strict named relation/missing-label errors, allocation/gates, reference transfer/teardown and 8-thread ownership passed");return 0;
}
"""
with tempfile.TemporaryDirectory(prefix="b2l-mach-filter-") as td:
    d=pathlib.Path(td);(d/"include/mach").mkdir(parents=True);(d/"include/mach/kern_return.h").write_text("typedef int kern_return_t;\n");(d/"test.c").write_text(prefix+'\n#include "'+str(provider)+'"\n'+suffix)
    compiler=os.getenv("CLANG") or shutil.which("clang");assert compiler
    subprocess.run([compiler,"-I"+str(d/"include"),"-I"+str(root/"xnu-overlay/osfmk"),"-std=c11","-Wall","-Wextra","-Werror","-g","-pthread","-fsanitize=address,undefined","-fno-omit-frame-pointer",str(d/"test.c"),"-o",str(d/"test")],check=True)
    subprocess.run([str(d/"test")],check=True)
