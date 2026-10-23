#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Compile the actual prepared native copyout body with faultable copy stubs."""
import subprocess,sys,tempfile
from pathlib import Path
xnu=Path(sys.argv[1]);source=(xnu/'osfmk/ipc/ipc_kmsg.c').read_text()
a=source.index('mach_msg_return_t\nipc_kmsg_put_to_user(');b=source.index('\n/** @} */',a)
body=source[a:b]
pre=r'''
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#define ARM64_BOARD_CONFIG_B2L 1
#define MACH_MSG_SUCCESS 0
#define MACH_RCV_INVALID_DATA 14
#define MACH_RCV_BODY_ERROR 15
#define MACH64_RCV_LINEAR_VECTOR 1
#define MACH64_MSG_VECTOR 2
#define MACH64_RCV_STACK 4
#define KERNEL_DEBUG_CONSTANT(...) ((void)0)
#define VM_KERNEL_ADDRPERM(a) (a)
#define MACHDBG_CODE(a,b) 0
#define DBG_MACH_IPC 0
#define MACH_IPC_KMSG_LINK 0
#define DBG_FUNC_NONE 0
#define MAX_TRAILER_SIZE 64
typedef unsigned mach_msg_size_t;typedef uint64_t mach_vm_address_t;
typedef int mach_msg_return_t;typedef uint64_t mach_msg_option64_t;
typedef void *ipc_kmsg_t;typedef void *vm_map_t;
typedef struct {uint32_t msgdh_size;}mach_msg_aux_header_t;
typedef struct {uint64_t recv_msg_addr,recv_aux_addr;unsigned recv_msg_size,recv_aux_size;}mach_msg_recv_bufs_t;
typedef struct {unsigned msgr_msg_size,msgr_trailer_size,msgr_aux_size;}mach_msg_recv_result_t;
typedef struct {void *dctx_uhdr,*dctx_udata,*dctx_aux_hdr;unsigned dctx_uhdr_size,dctx_udata_size,dctx_aux_size,dctx_trailer_size;}ikm_deflate_context_t;
static ikm_deflate_context_t ctx;static unsigned char dst[1024],src[256];
static int copies,fail_copy,freed,captured;static uint64_t observed,original;
static unsigned message_size,trailer_size;
static ikm_deflate_context_t ipc_kmsg_deflate(ipc_kmsg_t k,mach_msg_recv_result_t*m,uint64_t o,vm_map_t v){(void)k;(void)m;(void)o;(void)v;return ctx;}
static int copyoutmsg(void*s,uint64_t d,unsigned n){copies++;if(copies==fail_copy)return 14;assert(d>=(uintptr_t)dst&&d+n<=(uintptr_t)dst+sizeof(dst));memcpy((void*)(uintptr_t)d,s,n);return 0;}
static void ipc_kmsg_free(ipc_kmsg_t k){(void)k;freed++;}
static void b2l_diag_receive(uint64_t a,unsigned n,unsigned t,uint64_t b,unsigned c,uint64_t o){(void)c;(void)o;captured++;observed=a;original=b;message_size=n;trailer_size=t;}
'''
post=r'''
static mach_msg_recv_result_t result;
static mach_msg_return_t run(unsigned flags,unsigned message_capacity,unsigned aux_capacity,int fail,int native_result){
 memset(dst,0,sizeof(dst));memset(&result,0,sizeof(result));copies=freed=captured=0;fail_copy=fail;
 mach_msg_recv_bufs_t buffers={(uintptr_t)dst,(uintptr_t)(dst+700),message_capacity,aux_capacity};
 if(flags&MACH64_RCV_LINEAR_VECTOR){buffers.recv_aux_addr=0;buffers.recv_aux_size=0;}
 int mr=ipc_kmsg_put_to_user((void*)1,&buffers,&result,flags,NULL,native_result);assert(freed==1);return mr;
}
int main(void){
 for(unsigned i=0;i<sizeof(src);i++)src[i]=(unsigned char)(i+1);
 ctx=(ikm_deflate_context_t){.dctx_uhdr=src,.dctx_uhdr_size=24,.dctx_trailer_size=52};
 assert(run(0,512,0,0,0)==0&&captured==1&&observed==(uintptr_t)dst);assert(message_size==24&&trailer_size==52);
 unsigned flags=MACH64_RCV_LINEAR_VECTOR|MACH64_MSG_VECTOR|MACH64_RCV_STACK;
 assert(run(flags,512,0,0,0)==0&&captured==1&&observed==(uintptr_t)(dst+436)&&original==(uintptr_t)dst);
 assert(!memcmp(dst+436,src,76));
 ctx.dctx_udata=src+24;ctx.dctx_udata_size=32;ctx.dctx_aux_hdr=src+108;ctx.dctx_aux_size=8;
 assert(run(flags,512,0,0,0)==0&&captured==1&&observed==(uintptr_t)(dst+396)&&message_size==56);
 assert(!memcmp(dst+396,src,108)&&!memcmp(dst+504,src+108,8));
 for(int fail=1;fail<=3;fail++)assert(run(flags,512,0,fail,0)==MACH_RCV_INVALID_DATA&&captured==0&&result.msgr_msg_size==0);
 assert(run(flags,64,0,0,0)==MACH_RCV_INVALID_DATA&&copies==0&&captured==0);
 assert(run(flags,512,0,0,MACH_RCV_BODY_ERROR)==MACH_RCV_BODY_ERROR&&captured==0);
 puts("PASS native IPC copyout: scalar, stack vector, split payload+audit+aux, copy failures, overflow, native error preservation");
}
'''
with tempfile.TemporaryDirectory(prefix='b2l-ipc-copyout-test-')as td:
 p=Path(td);(p/'test.c').write_text(pre+body+post)
 subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
