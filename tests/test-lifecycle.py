#!/usr/bin/env python3
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1];s=(root/'xnu-overlay/bsd/kern/b2l_amfi.c').read_text()
def function(name):
 p=s.index(name+'(');start=s.rfind('\n',0,p)+1;a=s.index('{',p);level=1;q=a+1
 while level:
  if s[q]=='{':level+=1
  elif s[q]=='}':level-=1
  q+=1
 return s[start:q]
header=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#ifndef __unused
#define __unused __attribute__((unused))
#endif
#define CS_PLATFORM_BINARY 4u
#define MNT_ROOTFS 1
#define VREG 1
struct label {void*value;};
struct cred {struct label label;unsigned refs;};
struct proc {struct cred*cred;unsigned flags;};
struct mount {int flags;};
struct vnode {int kind;struct mount*mount;};
typedef struct cred*kauth_cred_t;
typedef struct proc*proc_t;
typedef struct mount*mount_t;
static void label_destroy(struct label*);
static int b2l_amfi_slot=0;
static bool b2l_amfi_registered,enabled=true,enforced=false;
static unsigned ent_live,register_calls,earlylogs;
struct ent {unsigned refs;bool platform;};
static void*b2l_ent_create(bool platform){struct ent*e=calloc(1,sizeof(*e));assert(e);e->refs=1;e->platform=platform;++ent_live;return e;}
static void b2l_ent_retain(void*p){if(p)++((struct ent*)p)->refs;}
static void b2l_ent_release(void*p){if(p){struct ent*e=p;assert(e->refs);if(!--e->refs){--ent_live;free(e);}}}
static bool b2l_ent_platform(const void*p){return p&&((const struct ent*)p)->platform;}
static bool b2l_arm64e_compat_enabled(void){return enabled;}
static bool cs_process_global_enforcement(void){return enforced;}
static int vnode_vtype(struct vnode*v){return v->kind;}
static mount_t vnode_mount(struct vnode*v){return v->mount;}
static int vfs_flags(mount_t m){return m->flags;}
static unsigned proc_getcsflags(proc_t p){return p->flags;}
static intptr_t mac_label_get(struct label*l,int slot){assert(slot==0);return (intptr_t)l->value;}
static void mac_label_set(struct label*l,int slot,intptr_t v){assert(slot==0);l->value=(void*)v;}
static struct label*mac_cred_label(kauth_cred_t c){return &c->label;}
static struct cred*swap_cred;
static void cred_drop(struct cred*c){assert(c&&c->refs);if(!--c->refs){label_destroy(&c->label);free(c);}}
static kauth_cred_t kauth_cred_proc_ref(proc_t p){struct cred*c=p->cred;++c->refs;if(swap_cred){p->cred=swap_cred;swap_cred=NULL;cred_drop(c);}return c;}
static void kauth_cred_unref(kauth_cred_t*c){cred_drop(*c);*c=NULL;}
static struct cred*cred_new(void*owned){struct cred*c=calloc(1,sizeof(*c));assert(c);c->refs=1;c->label.value=owned;return c;}
static void*next_ent;
static void*vnode_copy_ent(struct vnode*v,off_t o){(void)v;(void)o;void*e=next_ent;next_ent=NULL;return e;}
static bool b2l_ent_copy_xml(const void*e,void**out,size_t*n){if(out)*out=NULL;if(n)*n=0;if(!e||!out||!n)return false;*out=malloc(4);memcpy(*out,"XML",4);*n=4;return true;}
typedef struct{int identifier;}amfi_t;
static const amfi_t b2l_amfi={1},other_amfi={2};
static const amfi_t*amfi;
static void amfi_interface_register(const amfi_t*t){assert(!amfi);amfi=t;++register_calls;}
static void b2l_early_trace(const char*s){assert(s);++earlylogs;}
'''
names=['b2l_amfi_requested','b2l_amfi_active','b2l_amfi_vnode_is_platform','label_ent','label_replace','label_init','label_destroy','label_associate','proc_copy_ent','b2l_amfi_is_platform','b2l_amfi_proc_copy_xml','label_exec','register_interface']
main=r'''
int main(void){
 amfi=&other_amfi;register_interface();assert(!register_calls&&!b2l_amfi_registered);
 amfi=NULL;enabled=false;register_interface();assert(!register_calls);
 enabled=true;enforced=true;register_interface();assert(!register_calls);
 enforced=false;register_interface();assert(register_calls==1&&earlylogs==1&&amfi==&b2l_amfi&&b2l_amfi_active());
 struct mount m={MNT_ROOTFS};struct vnode v={VREG,&m};
 assert(b2l_amfi_vnode_is_platform(&v));v.kind=2;assert(!b2l_amfi_vnode_is_platform(&v));v.kind=VREG;m.flags=0;assert(!b2l_amfi_vnode_is_platform(&v));m.flags=MNT_ROOTFS;
 struct cred*a=cred_new(b2l_ent_create(true)),*b=cred_new(NULL);label_init(&b->label);label_associate(a,b);assert(((struct ent*)a->label.value)->refs==2);cred_drop(a);assert(((struct ent*)b->label.value)->refs==1);
 struct proc p={b,CS_PLATFORM_BINARY};assert(b2l_amfi_is_platform(&p));p.flags=0;assert(!b2l_amfi_is_platform(&p));p.flags=CS_PLATFORM_BINARY;
 void*out=(void*)1;size_t n=9;assert(!b2l_amfi_proc_copy_xml(&p,&out,&n)&&n==4);free(out);
 struct cred*replacement=cred_new(b2l_ent_create(false));swap_cred=replacement;void*held=proc_copy_ent(&p);assert(b2l_ent_platform(held)&&p.cred==replacement&&((struct ent*)held)->refs==1);b2l_ent_release(held);assert(!b2l_amfi_is_platform(&p));
 struct cred*c=cred_new(NULL);label_associate(p.cred,c);next_ent=b2l_ent_create(true);unsigned flags=0x100;int disjoint=1;
 assert(!label_exec(p.cred,c,&p,&v,0,NULL,NULL,NULL,NULL,&flags,NULL,0,&disjoint));assert(disjoint==1&&flags==(0x100|CS_PLATFORM_BINARY));
 /* Failed outer exec releases only its new model; old published cred survives. */
 cred_drop(c);assert(!b2l_ent_platform(p.cred->label.value));
 c=cred_new(NULL);label_associate(p.cred,c);flags=0x100|CS_PLATFORM_BINARY;next_ent=NULL;assert(label_exec(p.cred,c,&p,&v,0,NULL,NULL,NULL,NULL,&flags,NULL,0,&disjoint)==ENOMEM);assert(flags==0x100&&!c->label.value&&disjoint==1);cred_drop(c);
 c=cred_new(NULL);label_associate(p.cred,c);m.flags=0;next_ent=NULL;flags=CS_PLATFORM_BINARY;assert(!label_exec(p.cred,c,&p,&v,0,NULL,NULL,NULL,NULL,&flags,NULL,0,&disjoint));assert(!flags&&!c->label.value);cred_drop(c);m.flags=MNT_ROOTFS;
 /* A fresh rootfs exec can classify again after CLEARPLATFORM. */
 c=cred_new(NULL);next_ent=b2l_ent_create(true);flags=0;assert(!label_exec(p.cred,c,&p,&v,0,NULL,NULL,NULL,NULL,&flags,NULL,0,&disjoint));cred_drop(p.cred);p.cred=c;p.flags=flags;assert(b2l_amfi_is_platform(&p));
 enforced=true;assert(!b2l_amfi_is_platform(&p));enforced=false;cred_drop(p.cred);assert(ent_live==0);
 puts("Actual C metadata policy: registration gating, cred refs/fork/exec/exit, replacement, clear/reexec, OOM and disjoint PASS");return 0;}
'''
with tempfile.TemporaryDirectory(prefix='b2l-amfi-') as d:
 d=Path(d);(d/'test.c').write_text(header+'\n'.join(function(n) for n in names)+main)
 cc=subprocess.check_output(['xcrun','--find','clang'],text=True).strip();sdk=subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip()
 subprocess.run([cc,'-isysroot',sdk,'-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(d/'test.c'),'-o',str(d/'test')],check=True)
 subprocess.run([str(d/'test')],check=True)
