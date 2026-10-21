#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Verify the pinned real provider's registration, storage and dependency staging.

The full provider is compiled by XNU. This host test executes its actual table,
start/current-thread functions and _pthread_init body with hosted kernel hooks.
It is not a pthread syscall/locking scheduler emulator.
"""
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
import tempfile


def function(source,name):
    match=re.search(r'\n(?:__startup_func\s+)?(?:static\s+)?(?:void|kern_return_t|struct uthread\*)\s*\n?'+name+r'\([^;]*?\)\s*\{',source)
    if not match:
        raise AssertionError(name)
    end,depth=match.end(),1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[match.start():end]


def main():
    xnu=Path(sys.argv[1]).resolve();repo=Path(__file__).resolve().parent.parent
    provider=xnu/'bsd/pthread/b2l_libpthread'
    manifest=json.loads((provider/'b2l-provider.json').read_text())
    assert manifest['source_commit']=='1f4f5265b319111142f1bf3a27d4484ef5a98314'
    init=(provider/'kern/kern_init.c').read_text()
    support=(provider/'kern/kern_support.c').read_text()
    source='\n'.join((provider/'kern'/name).read_text() for name in ('kern_init.c','kern_support.c','kern_synch.c'))
    shims=(xnu/'bsd/pthread/pthread_shims.c').read_text()
    callbacks=shims[shims.index('static const struct pthread_callbacks_s pthread_callbacks = {'):]
    callbacks=callbacks[:callbacks.index('\n};')]
    used=set(re.findall(r'pthread_kern->(\w+)',source))
    present=set(re.findall(r'\.(\w+)\s*=',callbacks))
    assert not (used-present),used-present
    table=init[init.index('const struct pthread_functions_s pthread_internal_functions = {'):]
    table=table[:table.index('\n};')+3]
    fields=dict(re.findall(r'\.(\w+)\s*=\s*(\w+)',table))
    assert 'version' not in fields and 'bsdthread_register2' not in fields
    for required in ('pthread_init','pth_proc_hashinit','pth_proc_hashdelete','bsdthread_create',
                     'bsdthread_register','bsdthread_terminate','psynch_mutexwait','psynch_cvwait',
                     'psynch_rw_rdlock','workq_create_threadstack','workq_setup_thread'):
        assert required in fields
    layout=(provider/'kern/kern_internal.h').read_text()
    layout=layout[layout.index('struct ksyn_waitq_element {'):]
    layout=layout[:layout.index('\n};')+3]
    kernel=(xnu/'bsd/sys/pthread_internal.h').read_text()
    kernel=kernel[kernel.index('struct ksyn_waitq_element {'):]
    kernel=kernel[:kernel.index('\n};')+3].replace('struct ksyn_waitq_element','struct kernel_ksyn_waitq_element')
    reg=function((xnu/'bsd/pthread/b2l_pthread_init.c').read_text(),'b2l_pthread_register')
    pre=r'''
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/queue.h>
#define ARM64_BOARD_CONFIG_B2L 1
#define __startup_func
#ifndef __unused
#define __unused __attribute__((unused))
#endif
#define KERN_SUCCESS 0
#define KERN_FAILURE 5
#define panic(...) abort()
#define kprintf(...) (void)0
#define STARTUP(a,b,c)
typedef int kern_return_t;
typedef void kmod_info_t;
typedef void *thread_t;
struct uthread {int value;};
static struct uthread host_uthread={42};
static thread_t current_thread(void){return (void*)0x1234;}
struct pthread_callbacks_s {int version;struct uthread *(*get_bsdthread_info)(thread_t);};
typedef const struct pthread_callbacks_s *pthread_callbacks_t;
static struct uthread *host_get_bsdthread_info(thread_t t){assert(t==(void*)0x1234);return &host_uthread;}
static const struct pthread_callbacks_s pthread_callbacks={1,host_get_bsdthread_info};
'''
    pre+='\nstruct pthread_functions_s {int version;\n'+''.join(f'void (*{field})(void);\n' for field in fields)+'void (*bsdthread_register2)(void);};\n'
    pre+='typedef const struct pthread_functions_s *pthread_functions_t;\nstatic pthread_functions_t pthread_functions;\nvoid pthread_kext_register(pthread_functions_t,pthread_callbacks_t*);\n'
    pre+=layout+kernel
    pre+=r'''
static unsigned initialized;
static void *pthread_lck_grp_attr,*pthread_lck_grp,*pthread_lck_attr,*pthread_list_mlock,*psynch_thcall;
static int pthread_mutex_default_policy=99,sysctl__kern_pthread_mutex_default_policy;
static void *allocate(void){initialized++;return malloc(8);}
static void *lck_grp_attr_alloc_init(void){return allocate();}
static void *lck_grp_alloc_init(const char*n,void*a){assert(!strcmp(n,"pthread")&&a==pthread_lck_grp_attr);return allocate();}
static void *lck_attr_alloc_init(void){return allocate();}
static void *lck_mtx_alloc_init(void*g,void*a){assert(g==pthread_lck_grp&&a==pthread_lck_attr);return allocate();}
static void pth_global_hashinit(void){initialized++;}
static void psynch_wq_cleanup(void){}
static void *thread_call_allocate(void(*f)(void),void*a){assert(f==psynch_wq_cleanup&&!a);return allocate();}
static int PE_parse_boot_argn(const char*n,void*p,size_t s){assert(!strcmp(n,"pthread_mutex_default_policy")&&p!=NULL&&s==sizeof(int));return 0;}
static void sysctl_register_oid(void*p){assert(p==&sysctl__kern_pthread_mutex_default_policy);initialized++;}
'''
    # Actual upstream init owns the real initialization sequence; other table
    # targets are just addresses in this isolated registration test.
    pre+=function(support,'_pthread_init')
    for target in sorted(set(fields.values())-{'_pthread_init'}):
        pre+=f'\nstatic void {target}(void){{}}\n'
    real='\n'.join(line for line in init.splitlines() if not line.startswith('#include'))
    # Only separate provider-owned symbols from their XNU-side counterparts.
    aliases='\n#define pthread_kern b2l_pthread_kern\n#define current_uthread b2l_pthread_current_uthread\n#define pthread_start b2l_pthread_start\n#define pthread_stop b2l_pthread_stop\n'
    body=aliases+real+'\n#undef pthread_kern\n#undef current_uthread\n#undef pthread_start\n#undef pthread_stop\n'
    body+=function(shims,'pthread_kext_register')+function(shims,'pthread_init')+reg
    post=r'''
static void expect_abort(void(*f)(void)){
 pid_t child=fork();assert(child>=0);
 if(child==0){freopen("/dev/null","w",stderr);f();_exit(0);}
 int status;assert(waitpid(child,&status,0)==child);assert(WIFSIGNALED(status)&&WTERMSIG(status)==SIGABRT);
}
int main(void){
 _Static_assert(sizeof(struct ksyn_waitq_element)==56,"provider layout");
 _Static_assert(_Alignof(struct ksyn_waitq_element)==8,"provider alignment");
 _Static_assert(offsetof(struct ksyn_waitq_element,kwe_uth)==48,"provider trailing field");
 _Static_assert(sizeof(struct kernel_ksyn_waitq_element)==56,"kernel opaque storage");
 _Static_assert(_Alignof(struct kernel_ksyn_waitq_element)==8,"kernel opaque alignment");
 struct {uint64_t before;struct kernel_ksyn_waitq_element element;uint64_t after;} wrapped={.before=0x1234,.after=0x5678};
 struct ksyn_waitq_element *element=(void*)&wrapped.element;element->kwe_uth=&host_uthread;
 assert(wrapped.before==0x1234&&wrapped.after==0x5678);
 expect_abort(pthread_init); /* Original missing-provider panic remains real. */
 b2l_pthread_register();assert(b2l_pthread_kern==&pthread_callbacks);
 assert(pthread_functions==&pthread_internal_functions);
 assert(pthread_functions->version==0&&pthread_functions->bsdthread_register2==NULL);
 assert(b2l_pthread_current_uthread()==&host_uthread);
 pthread_init();assert(initialized==7&&pthread_mutex_default_policy==99);
 expect_abort(b2l_pthread_register); /* Original duplicate-registration protection. */
 free(pthread_lck_grp_attr);free(pthread_lck_grp);free(pthread_lck_attr);free(pthread_list_mlock);free(psynch_thcall);
 puts("Pinned real pthread registration/init flow and 56-byte ABI: ASan/UBSan passed");
}
'''
    with tempfile.TemporaryDirectory(prefix='b2l-pthread-test-') as temporary:
        root=Path(temporary);src=root/'test.c';binary=root/'test'
        src.write_text(pre+body+post)
        subprocess.run(shlex.split(os.environ.get('CC','clang'))+['-std=c11','-g','-O1','-fsanitize=address,undefined',str(src),'-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True)
        # Verify real staging hashes and refusal of edited dependencies, without
        # touching the candidate build's source or fetched checkout.
        clone=root/'xnu';(clone/'bsd/pthread').mkdir(parents=True)
        (clone/'.git').write_text('gitdir: '+str(xnu/'.git')+'\n')
        target=clone/'bsd/pthread/b2l_libpthread';shutil.copytree(provider,target)
        stage=['python3',str(repo/'scripts/stage-libpthread.py'),str(clone)]
        subprocess.run(stage,check=True)
        changed=target/'kern/kern_init.c';changed.write_text(changed.read_text()+'\n/* changed */\n')
        result=subprocess.run(stage,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
        assert result.returncode!=0 and 'modified' in result.stderr
    print(f'All {len(used)} required kernel callbacks present; legacy version-0 table preserved; modified dependency rejected')


if __name__=='__main__':
    main()
