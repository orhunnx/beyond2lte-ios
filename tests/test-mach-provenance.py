#!/usr/bin/env python3
"""Compile the patched native validation function with lock/lifetime mocks."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
xnu=Path(os.environ.get('XNU_PATH',root/'out/build-env-0063/src/xnu'))
source=xnu/'osfmk/ipc/mach_port.c'
with tempfile.TemporaryDirectory(prefix='b2l-mach-diag-') as td:
 d=Path(td);target=d/'osfmk/ipc/mach_port.c';target.parent.mkdir(parents=True);shutil.copyfile(source,target)
 patch=root/'patches/0045-b2l-mach-provenance-diagnostics.patch'
 if 'B2L_MACH_VALIDATE_RETURN' not in target.read_text():
  subprocess.run(['patch','--silent','--batch','-p1','-d',str(d),'-i',str(patch)],check=True)
 text=target.read_text();start=text.index('kern_return_t\nmach_port_is_connection_for_service(');end=text.index('\n#if CONFIG_SERVICE_PORT_INFO',start);body=text[start:end]
 # Verify constructor emits only after native unlock and snapshots while owned.
 construct=text[text.index('mach_port_construct(\n'):text.index('mach_port_destruct(',text.index('mach_port_construct(\n'))]
 assert construct.index('b2l_construct_port = VM_KERNEL_ADDRPERM')<construct.index('ip_mq_unlock(port);')<construct.index('b2l_diag_mach_construct(')
 mock=r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#define ARM64_BOARD_CONFIG_B2L 1
#define IS_NULL 0
#define KERN_SUCCESS 0
#define KERN_INVALID_ARGUMENT 4
#define KERN_INVALID_TASK 16
#define KERN_INVALID_NAME 15
#define KERN_NOT_SUPPORTED 46
#define KERN_INVALID_CAPABILITY 20
#define MACH_PORT_VALID(n) ((n)!=0&&(n)!=UINT32_MAX)
#define MACH_MSG_FILTER_CALLBACKS_VERSION_1 1
#define IOT_CONNECTION_PORT 12
#define VM_KERNEL_ADDRPERM(x) ((uint64_t)(x)^UINT64_C(0x31415926))
typedef int kern_return_t;typedef int ipc_space_t;typedef uint32_t mach_port_name_t;
struct ipc_conn_port_label{int references;};
struct service_label {struct ipc_conn_port_label*ispl_sblabel;};
typedef struct{unsigned io_type;struct service_label*iol_service;struct ipc_conn_port_label*iol_connection;} ipc_object_label_t;
struct port{ipc_object_label_t label;};typedef struct port*mach_port_t;
static struct ipc_conn_port_label a,b;static struct service_label sl={&a};
static struct port service,connection;static int lock_depth,filter_ready,service_error,connection_error,callback_result,calls,retains,releases;static unsigned stage;static uint64_t sk,ck,sp,cp;static unsigned st,ct;
static int mach_msg_filter_at_least(int v){assert(v==1);return filter_ready;}
static int ipc_port_translate_receive(int space,unsigned name,mach_port_t*out){assert(space==1&&!lock_depth);int e=name==11?service_error:connection_error;if(e)return e;*out=name==11?&service:&connection;lock_depth=1;return 0;}
static ipc_object_label_t ip_label_get(mach_port_t p){assert(lock_depth==1);return p->label;}
static int ip_is_any_service_port_type(unsigned t){return t==10;}
static void ip_mq_unlock_label_put(mach_port_t p,ipc_object_label_t*l){(void)p;(void)l;assert(lock_depth==1);lock_depth=0;}
static void mach_msg_filter_retain_sblabel_callback(void*p){assert(p&&lock_depth==1);retains++;}
static void mach_msg_filter_dealloc_service_port_sblabel_callback(void*p){assert(p&&!lock_depth);releases++;}
static int mach_msg_filter_get_connection_port_filter_policy_callback(void*s,void*c,uint64_t*out){assert(!lock_depth&&s);releases+=!!s+!!c;*out=99;sl.ispl_sblabel=NULL;connection.label.iol_connection=NULL;return callback_result;}
static void b2l_diag_mach_validate(unsigned c,unsigned s,unsigned svctype,uint64_t svcport,uint64_t svckey,unsigned conntype,uint64_t connport,uint64_t connkey,int result,unsigned phase){assert(!lock_depth&&c!=12345&&s!=12345);(void)result;calls++;stage=phase;sk=svckey;ck=connkey;sp=svcport;cp=connport;st=svctype;ct=conntype;}
static void setup(void){lock_depth=0;filter_ready=1;service_error=connection_error=callback_result=calls=retains=releases=0;sl.ispl_sblabel=&a;service.label=(ipc_object_label_t){10,&sl,NULL};connection.label=(ipc_object_label_t){12,NULL,&a};stage=99;sk=ck=sp=cp=0;}
'''
 test=r'''
int main(void){uint64_t policy=777;int r;
setup();assert(mach_port_is_connection_for_service(0,22,11,&policy)==KERN_INVALID_TASK&&stage==0&&calls==1&&policy==777);
setup();assert(mach_port_is_connection_for_service(1,0,11,&policy)==KERN_INVALID_NAME&&stage==0);
setup();assert(mach_port_is_connection_for_service(1,22,11,NULL)==KERN_INVALID_ARGUMENT&&stage==0);
setup();filter_ready=0;assert(mach_port_is_connection_for_service(1,22,11,&policy)==KERN_NOT_SUPPORTED&&stage==0);
setup();service_error=17;assert(mach_port_is_connection_for_service(1,22,11,&policy)==17&&stage==1);
setup();service.label.io_type=7;assert(mach_port_is_connection_for_service(1,22,11,&policy)==KERN_INVALID_CAPABILITY&&stage==2&&st==7&&sp==VM_KERNEL_ADDRPERM((uintptr_t)&service));
setup();sl.ispl_sblabel=NULL;assert(!mach_port_is_connection_for_service(1,22,11,&policy)&&stage==3&&policy==0&&sk==0&&retains==0);
setup();connection_error=18;assert(mach_port_is_connection_for_service(1,22,11,&policy)==18&&stage==4&&retains==1&&releases==1);
setup();connection.label.io_type=7;callback_result=4;r=mach_port_is_connection_for_service(1,22,11,&policy);assert(r==4&&stage==5&&ck==0&&ct==7&&cp==VM_KERNEL_ADDRPERM((uintptr_t)&connection)&&retains==1&&releases==1&&policy==99);
setup();assert(!mach_port_is_connection_for_service(1,22,11,&policy)&&stage==5&&sk==ck&&sk==VM_KERNEL_ADDRPERM((uintptr_t)&a)&&retains==2&&releases==2);
setup();connection.label.iol_connection=&b;callback_result=4;assert(mach_port_is_connection_for_service(1,22,11,&policy)==4&&stage==5&&sk!=ck&&retains==2&&releases==2);
puts("Patched native Mach validation: all return stages, unlocked diagnostics, retained-reference snapshots and unchanged results passed");}
'''
 (d/'test.c').write_text(mock+body+test)
 cc=os.getenv('CLANG') or shutil.which('clang');assert cc
 subprocess.run([cc,'-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(d/'test.c'),'-o',str(d/'test')],check=True)
 subprocess.run([str(d/'test')],check=True)
