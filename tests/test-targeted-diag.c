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
static int process64=1;
static proc_t current_proc(void){return &process;}static int proc_is64bit(proc_t p){(void)p;return process64;}
static int pid=28;
static int proc_selfpid(void){return pid;}static void *current_thread(void){return (void*)1;}
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
static unsigned backtrace(uintptr_t *bt,unsigned n,void *ctl,void *info){(void)ctl;(void)info;assert(n>=2);bt[0]=0xffff000000001000;bt[1]=0xffff000000002000;return 2;}
#define SYS___mac_syscall 381
#define SYS_csops 169
#define SYS_csops_audittoken 170
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
static void clear(void){used=0;output[0]=0;memset(ipc_samples,0,sizeof(ipc_samples));memset(ipc_seen,0,sizeof(ipc_seen));memset(process_samples,0,sizeof(process_samples));memset(tcc_samples,0,sizeof(tcc_samples));raw_budget=4*1024*1024;}
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
 // MCM captures actual iOS26 bootstrap/request/reply IDs after dyld chatter.
 for(unsigned i=0;i<40;i++)b2l_diag_ipc(0,ptr,24,0,1,0,0,0);
 b2l_diag_ipc(0,ptr,24,0,0x400000cf,0,0,0);
 b2l_diag_ipc(1,ptr,44,0,0,0,0,0);
 assert(strstr(output,"id=0x400000cf"));clear();
 // Per-process saturation does not starve another MCM process or a new lsd.
 for(unsigned i=0;i<600;i++)b2l_diag_ipc(0,ptr,24,0,1,0,0,0);
 assert(process_samples[0][0].ipc[0]==600);used=0;output[0]=0;pid=100;
 b2l_diag_ipc(0,ptr,24,0,0x400000cf,0,0,0);assert(strstr(output,"pid=100"));clear();
 // Discovery chatter does not exhaust later GUI XPC admission counters.
 strcpy(process.p_comm,"SpringBoard");
 for(unsigned i=0;i<200;i++)b2l_diag_ipc(0,ptr,24,0,1,0,0,0);
 assert(ipc_samples[0][0]==24);b2l_diag_ipc(0,ptr,24,0,0x10000000,0,0,0);assert(ipc_samples[0][0]==25);clear();
 strcpy(process.p_comm,"SpringBoard");b2l_diag_wait("mach-recv",2,3,4);assert(strstr(output,"WAITBT"));size_t old=used;
 b2l_diag_wait("mach-recv",2,3,4);assert(used==old);
 for(unsigned i=0;i<15;i++){now+=5000000000;b2l_diag_wait("mach-recv",2,3,4);}assert(slots[0][0].count==8);clear();
 strcpy(process.p_comm,"containermanage");
 uint64_t query[5]={111,0,ptr,4,0};h[0]=0x24000001;
 b2l_diag_syscall(SYS_csops_audittoken,0,query);
 assert(strstr(output,"target=111 op=0")&&strstr(output,"status=0x24000001"));clear();
 query[1]=5;b2l_diag_syscall(SYS_csops_audittoken,3,query);
 assert(strstr(output,"op=5")&&strstr(output,"error=3"));clear();
 strcpy(process.p_comm,"SpringBoard");
 uint64_t args[2]={ptr,0};strcpy((char*)region,"/private/var/containers/test");
 b2l_diag_syscall(SYS_open,2,args);assert(strstr(output,"SYSPATH")&&strstr(output,"error=2"));old=used;
 b2l_diag_syscall(SYS_open,0,args);assert(old==used);
 // Actual copyout address differs from the stack-vector allocation base.
 clear();strcpy(process.p_comm,"containermanage");memset(region,0,sizeof(region));
 uint32_t *delivered=(void*)(region+2048);delivered[0]=0x1212;delivered[1]=24;delivered[3]=0x1607;delivered[5]=0x40000000;
 uint32_t *tr=(void*)(region+2048+24);tr[0]=0;tr[1]=52;tr[10]=110;
 b2l_diag_receive(ptr+2048,24,52,ptr,4096,0x123);
 assert(strstr(output,"id=0x40000000")&&strstr(output,"audit=0,0,0,0,0,110,0,0"));
 clear();requested=0;b2l_diag_receive(ptr+2048,24,52,ptr,4096,0);assert(!used);requested=1;
 // Track only an lsd send-once reply endpoint, then observe another process
 // destroying it. User stack reading must not happen inside the notify hook.
 strcpy(process.p_comm,"lsd");
 b2l_diag_ports(0,0x40000000,18u<<8,0x11110000,0x22220000,0x1503,0x1607);
 clear();strcpy(process.p_comm,"other");
 b2l_diag_sonce(0x33330000,0,0);assert(!used);
 b2l_diag_sonce(0x22220000,0x1607,55);
 assert(strstr(output,"tracked=1")&&strstr(output,"SONCEKBT")&&!strstr(output,"SONCEUBT"));
 b2l_diag_checkpoint();assert(strstr(output,"SONCEUBT"));old=used;
 b2l_diag_checkpoint();assert(used==old);
 // Capture native launch-constraint results independently of generic error
 // budget, without changing any user argument or result byte.
 clear();strcpy(process.p_comm,"backboardd");memset(region,0,sizeof(region));
 strcpy((char*)region,"AMFI");uint64_t *wire=(void*)(region+64);
 wire[4]=ptr+256;wire[5]=16;wire[6]=ptr+1024;wire[7]=0x404;
 memcpy(region+256,"constraint-data",16);uint32_t failure=3;memcpy(region+1024,&failure,4);
 uint64_t macargs[3]={ptr,0x66,ptr+64};unsigned char snapshot[8192];memcpy(snapshot,region,sizeof(region));
 b2l_diag_syscall(SYS___mac_syscall,45,macargs);
 assert(strstr(output,"B2L AMFI:")&&strstr(output,"result_code=3")&&strstr(output,"part=amfi_constraint"));
 assert(!memcmp(snapshot,region,sizeof(region)));clear();
 macargs[2]=UINT64_MAX;b2l_diag_syscall(SYS___mac_syscall,14,macargs);
 assert(strstr(output,"args_copy=14")&&!strstr(output,"part=amfi_args"));clear();
 macargs[2]=ptr+64;wire[5]=16385;b2l_diag_syscall(SYS___mac_syscall,45,macargs);
 assert(strstr(output,"constraint_bytes=16385")&&!strstr(output,"part=amfi_constraint"));clear();
 for(unsigned i=0;i<12;i++)b2l_diag_syscall(SYS___mac_syscall,45,macargs);
 assert(amfi_samples>=8);clear();b2l_diag_syscall(SYS___mac_syscall,45,macargs);assert(!strstr(output,"B2L AMFI:"));
 // TCC metadata diagnostics keep native responses unchanged and never enroll
 // these processes in the general role/raw IPC capture.
 clear();strcpy(process.p_comm,"assetsd");assert(role()==-1);
 memset(region,0,sizeof(region));region[0]=0xfa;region[1]=0xde;region[2]=0x71;region[3]=0x72;region[6]=0x12;region[7]=0x34;
 uint64_t tccargs[5]={555,16,ptr,8,0};memcpy(snapshot,region,sizeof(region));
 requested=0;b2l_diag_syscall(SYS_csops_audittoken,34,tccargs);assert(!used);requested=1;
 process64=0;b2l_diag_syscall(SYS_csops_audittoken,34,tccargs);assert(!used);process64=1;
 strcpy(process.p_comm,"assetsd-other");b2l_diag_syscall(SYS_csops_audittoken,34,tccargs);assert(!used);
 strcpy(process.p_comm,"assetsd");b2l_diag_syscall(SYS_csops_audittoken,34,tccargs);
 assert(strstr(output,"caller=assetsd")&&strstr(output,"target=555 audit=1 op=16")&&strstr(output,"magic=0xfade7172 length=4660 copied=0"));
 assert(!memcmp(snapshot,region,sizeof(region))&&!strstr(output,"IPCDATA"));clear();
 tccargs[1]=7;tccargs[3]=7;b2l_diag_syscall(SYS_csops,0,tccargs);assert(strstr(output,"magic=0x0 length=0 copied=-1"));clear();
 tccargs[3]=8;tccargs[2]=UINT64_MAX;b2l_diag_syscall(SYS_csops,34,tccargs);assert(strstr(output,"copied=14"));clear();
 tccargs[2]=ptr;tccargs[1]=0;tccargs[3]=4;h[0]=0x24000001;
 b2l_diag_syscall(SYS_csops,0,tccargs);assert(strstr(output,"status=0x24000001")&&strstr(output,"copied=0"));clear();
 strcpy((char*)region,"OTHER");macargs[0]=ptr;macargs[1]=0x99;
 b2l_diag_syscall(SYS___mac_syscall,45,macargs);assert(!used);
 strcpy((char*)region,"AMFI");b2l_diag_syscall(SYS___mac_syscall,0,macargs);assert(!used);
 b2l_diag_syscall(SYS___mac_syscall,45,macargs);assert(strstr(output,"TCCMAC:")&&strstr(output,"policy=AMFI op=153 error=45"));clear();
 for(unsigned i=0;i<80;i++)b2l_diag_syscall(SYS_csops,0,tccargs);
 assert(tcc_samples[0]==80);old=used;b2l_diag_syscall(SYS_csops,0,tccargs);assert(old==used);
 used=0;output[0]=0;strcpy(process.p_comm,"tccd");b2l_diag_syscall(SYS_csops,0,tccargs);
 assert(strstr(output,"caller=tccd")&&tcc_samples[1]==1);clear();
 puts("PASS actual targeted diagnostics: gating, current-thread snapshots, limits, copy faults, OOL bounds, XPC admission and path errors");
}
