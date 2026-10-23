/* SPDX-License-Identifier: BSD-2-Clause */
#define B2L_DIAG_HOST_TEST 1
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
struct proc {char p_comm[32];};typedef struct proc *proc_t;
static struct proc process={"SpringBoard"};static int requested=1;static uint64_t now=1000000000;
static unsigned char region[8192];static char output[200000];static size_t used;
static proc_t current_proc(void){return &process;}static int proc_is64bit(proc_t p){(void)p;return 1;}
static int proc_selfpid(void){return 28;}static void *current_thread(void){return (void*)1;}
static uint64_t thread_tid(void *p){(void)p;return 41;}
static uint64_t mach_absolute_time(void){return now;}
static void absolutetime_to_nanoseconds(uint64_t t,uint64_t *v){*v=t;}
static int PE_parse_boot_argn(const char*n,void*p,int len){(void)n;(void)len;memcpy(p,&requested,4);return 1;}
static int copyin(uint64_t a,void *out,size_t n){if(a<(uintptr_t)region||a>(uintptr_t)region+sizeof(region)||n>(uintptr_t)region+sizeof(region)-a)return 14;memcpy(out,(void*)(uintptr_t)a,n);return 0;}
static int copyinstr(uint64_t a,char*out,size_t n,size_t*len){for(size_t i=0;i<n;i++){if(copyin(a+i,out+i,1))return 14;if(!out[i]){*len=i+1;return 0;}}return 36;}
static void b2l_log_append_bytes(const void*p,size_t n){assert(used+n<sizeof(output));memcpy(output+used,p,n);used+=n;output[used]=0;}
typedef struct {uint64_t pc;}arm_saved_state_t;
static arm_saved_state_t regs={0x100000000};
static arm_saved_state_t *get_user_regs(void *t){(void)t;return &regs;}
static uint64_t get_saved_state_pc(arm_saved_state_t *r){return r->pc;}
struct backtrace_user_info {int btui_error;};
#define BTUINFO_INIT {0}
static unsigned backtrace_user(uintptr_t *bt,unsigned n,void *c,struct backtrace_user_info *i){(void)c;(void)i;assert(n>=2);bt[0]=0x100000000;bt[1]=0x180000000;return 2;}
#define SYS_open 5
#define SYS_stat64 338
#define SYS_lstat64 340
#define SYS_access 33
#define SYS_mkdir 136
#define SYS_getattrlist 220
#define SYS_rename 128
#define SYS_renamex_np 488
#define SYS_openat 463
#define SYS_mkdirat 475
#define SYS_fstatat64 470
#define SYS_renameat 465
#define SYS_renameatx_np 489
#include "../xnu-overlay/bsd/kern/b2l_diag.c"
static void clear(void){used=0;output[0]=0;memset(ipc_samples,0,sizeof(ipc_samples));memset(ipc_seen,0,sizeof(ipc_seen));raw_budget=512*1024;}
int main(void){
 uint32_t *h=(void*)region;uint64_t ptr=(uintptr_t)region;
 requested=0;b2l_diag_ipc(0,ptr,64,0,1,0,0,0);assert(!used);
 requested=1;strcpy(process.p_comm,"other");b2l_diag_wait("syscall",1,2,3);assert(!used);
 strcpy(process.p_comm,"containermanage");memcpy(region+28,"container-identifier",21);
 b2l_diag_ipc(0,ptr,64,0,0x10000000,10,20,0);assert(strstr(output,"role=2")&&strstr(output,"part=inline"));clear();
 // A 64-bit OOL descriptor, following the 24-byte header + body count.
 memset(region,0,sizeof(region));h[0]=0x80000000;h[1]=44;h[5]=0x10000000;h[6]=1;
 uint64_t a=ptr+128;uint32_t n=16;memcpy(region+28,&a,8);region[39]=1;memcpy(region+40,&n,4);memcpy(region+128,"container-error",16);
 b2l_diag_ipc(1,ptr,44,0,0,0,0,0);assert(strstr(output,"IPCOOL")&&strstr(output,"part=ool")&&strstr(output,"636f6e7461696e65722d6572726f72"));clear();
 h[1]=45;b2l_diag_ipc(1,ptr,44,0,0,0,0,0);assert(!used);h[1]=44;
 region[39]=255;b2l_diag_ipc(1,ptr,44,0,0,0,0,0);assert(!strstr(output,"IPCOOL"));clear();
 region[39]=1;h[6]=17;b2l_diag_ipc(1,ptr,44,0,0,0,0,0);assert(!strstr(output,"IPCOOL"));clear();h[6]=1;
 a=UINT64_MAX-10;memcpy(region+28,&a,8);b2l_diag_ipc(1,ptr,44,0,0,0,0,0);assert(strstr(output,"IPCOOL")&&!strstr(output,"part=ool"));clear();
 raw_budget=9;b2l_diag_ipc(1,ptr,44,0,0,0,0,0);assert(raw_budget==0&&strstr(output,"captured=9"));clear();
 // Discovery chatter does not exhaust later XPC admission counters.
 for(unsigned i=0;i<200;i++)b2l_diag_ipc(0,ptr,24,0,1,0,0,0);
 assert(ipc_samples[2][0]==24);b2l_diag_ipc(0,ptr,24,0,0x10000000,0,0,0);assert(ipc_samples[2][0]==25);clear();
 strcpy(process.p_comm,"SpringBoard");b2l_diag_wait("mach-recv",2,3,4);assert(strstr(output,"WAITBT"));size_t old=used;
 b2l_diag_wait("mach-recv",2,3,4);assert(used==old);
 for(unsigned i=0;i<15;i++){now+=5000000000;b2l_diag_wait("mach-recv",2,3,4);}assert(slots[0][0].count==8);clear();
 uint64_t args[2]={ptr,0};strcpy((char*)region,"/private/var/containers/test");
 b2l_diag_syscall(SYS_open,2,args);assert(strstr(output,"SYSPATH")&&strstr(output,"error=2"));old=used;
 b2l_diag_syscall(SYS_open,0,args);assert(old==used);
 puts("PASS actual targeted diagnostics: gating, current-thread snapshots, limits, copy faults, OOL bounds, XPC admission and path errors");
}
