/* SPDX-License-Identifier: BSD-2-Clause
 * Opt-in diagnostic snapshots of current-thread boundaries, not a debugger.
 * Never suspend tasks, alter messages or replace syscall/security results.
 */
#ifndef B2L_DIAG_HOST_TEST
#include <sys/proc_internal.h>
#include <sys/systm.h>
#include <sys/syscall.h>
#include <kern/thread.h>
#include <kern/backtrace.h>
#include <kern/clock.h>
#include <arm/thread.h>
#include <mach/mach_time.h>
#include <pexpert/pexpert.h>
#include <arm64/b2l_diag.h>
#include <arm64/b2l_persistent_log.h>
#endif

void b2l_diag_record(const char *fmt,...)
{
    char text[640];va_list ap;va_start(ap,fmt);
    int n=vsnprintf(text,sizeof(text),fmt,ap);va_end(ap);
    if(n<=0)return;
    size_t used=(size_t)n<sizeof(text)?(size_t)n:sizeof(text)-1;
    if((size_t)n>=sizeof(text))text[used-1]='\n';
    b2l_log_append_bytes(text,used);
}
static int role(void)
{
    proc_t p=current_proc();if(!p||!proc_is64bit(p))return -1;
    int r=-1;
    if(!strcmp(p->p_comm,"SpringBoard"))r=0;
    else if(!strcmp(p->p_comm,"backboardd"))r=1;
    else if(!strncmp(p->p_comm,"containermanage",14))r=2;
    else if(!strcmp(p->p_comm,"lsd"))r=3;
    if(r<0)return -1;
    int requested=0;PE_parse_boot_argn("b2l_diag",&requested,sizeof(requested));
    return requested==1?r:-1;
}
struct sample_slot {uint64_t tid,last;unsigned count;};
static struct sample_slot slots[2][16];
/* Current thread only, before potentially blocking native calls. A thread
 * which never returns still leaves its entry PC/stack in the persistent log. */
void b2l_diag_wait(const char *kind,uint64_t a,uint64_t b,uint64_t c)
{
    int r=role();if(r<0||r>1)return;
    uint64_t tid=thread_tid(current_thread()),ns=0;
    absolutetime_to_nanoseconds(mach_absolute_time(),&ns);
    uint64_t sec=ns/1000000000+1;
    struct sample_slot *slot=NULL;
    for(unsigned i=0;i<16;i++){
        uint64_t old=__atomic_load_n(&slots[r][i].tid,__ATOMIC_RELAXED);
        if(old==tid){slot=&slots[r][i];break;}
        if(!old&&__atomic_compare_exchange_n(&slots[r][i].tid,&old,tid,1,__ATOMIC_RELAXED,__ATOMIC_RELAXED)){slot=&slots[r][i];break;}
    }
    if(!slot||slot->count>=8||(slot->last&&sec-slot->last<5))return;
    /* Each tid owns its slot. No remote stack reads or shared sample buffer. */
    slot->last=sec;unsigned sample=++slot->count;int pid=proc_selfpid();
    arm_saved_state_t *regs=get_user_regs(current_thread());
    b2l_diag_record("B2L WAIT: pid=%d tid=%llu sample=%u sec=%llu kind=%s a=0x%llx b=0x%llx c=0x%llx pc=0x%llx\n",
        pid,tid,sample,sec,kind,a,b,c,regs?(uint64_t)get_saved_state_pc(regs):0);
    uintptr_t bt[12];struct backtrace_user_info bi=BTUINFO_INIT;
    unsigned n=backtrace_user(bt,12,NULL,&bi);
    b2l_diag_record("B2L WAITBT: pid=%d tid=%llu sample=%u frames=%u error=%d\n",pid,tid,sample,n,bi.btui_error);
    for(unsigned i=0;i<n&&i<12;i++)b2l_diag_record("B2L WAITBT: pid=%d tid=%llu sample=%u frame=%u pc=0x%llx\n",pid,tid,sample,i,(uint64_t)bt[i]);
}
static unsigned ipc_samples[4][2],ipc_seen[4][2],sys_samples[4];
static uint64_t message_sequence;
static unsigned raw_budget=512*1024;
static unsigned raw_reserve(unsigned n)
{
    unsigned old=__atomic_load_n(&raw_budget,__ATOMIC_RELAXED);
    for(unsigned i=0;i<4;i++){
        unsigned take=n<old?n:old;if(!take)return 0;
        if(__atomic_compare_exchange_n(&raw_budget,&old,old-take,1,__ATOMIC_RELAXED,__ATOMIC_RELAXED))return take;
    }return 0;
}
static void bytes(uint64_t seq,const char *part,uint64_t addr,unsigned n)
{
    if(!addr||addr>UINT64_MAX-n)return;
    n=raw_reserve(n);
    b2l_diag_record("B2L IPCDATA: seq=%llu part=%s captured=%u\n",seq,part,n);
    for(unsigned off=0;off<n;off+=64){
        uint8_t buf[64];char hex[129];unsigned take=n-off<64?n-off:64;
        if(copyin(addr+off,buf,take)){
            b2l_diag_record("B2L IPCDATA: seq=%llu part=%s off=%u copyin_error=1\n",seq,part,off);break;
        }
        static const char digits[]="0123456789abcdef";
        for(unsigned i=0;i<take;i++){hex[i*2]=digits[buf[i]>>4];hex[i*2+1]=digits[buf[i]&15];}hex[take*2]=0;
        b2l_diag_record("B2L IPCDATA: seq=%llu part=%s off=%u hex=%s\n",seq,part,off,hex);
    }
}
/* Send arguments describe the current user wire ABI, including vector calls.
 * On receive, header/descriptor count are re-read only after native copyout.
 * Copy failures and truncated payloads are diagnostics, never new IPC errors. */
void b2l_diag_ipc(int rx,uint64_t addr,uint32_t n,uint32_t bits,int32_t id,
    uint32_t remote,uint32_t local,uint32_t count)
{
    int r=role();if(r<0||rx<0||rx>1||n<24||!addr||addr>UINT64_MAX-n)return;
    uint32_t h[7]={0};
    if(rx){
        if(copyin(addr,h,24)||h[1]<24||h[1]>n)return;
        bits=h[0];n=h[1];remote=h[2];local=h[3];id=(int32_t)h[5];
        if((bits&0x80000000u)&&n>=28&&!copyin(addr+24,h,4))count=h[0];
    }
    /* Capture discovery messages, then restrict later payloads to libxpc's
     * request/reply IDs. Numeric IDs are retained for offline interpretation. */
    unsigned seen=__atomic_fetch_add(&ipc_seen[r][rx],1,__ATOMIC_RELAXED);
    if(seen>=24&&id!=0x10000000&&id!=0x10000001)return;
    unsigned sample=__atomic_fetch_add(&ipc_samples[r][rx],1,__ATOMIC_RELAXED);
    if(sample>=128)return;
    uint64_t seq=__atomic_fetch_add(&message_sequence,1,__ATOMIC_RELAXED)+1;
    b2l_diag_record("B2L IPC: seq=%llu dir=%s role=%d pid=%d tid=%llu id=0x%x bits=0x%x remote=0x%x local=0x%x bytes=%u descriptors=%u\n",
        seq,rx?"rx":"tx",r,proc_selfpid(),thread_tid(current_thread()),id,bits,remote,local,n,count);
    bytes(seq,"inline",addr,n<1024?n:1024);
    if(!(bits&0x80000000u)||n<28||count>16)return;
    uint32_t off=28;
    for(unsigned i=0;i<count;i++){
        uint8_t d[16]={0};if(off>n||n-off<12||copyin(addr+off,d,12))break;
        uint8_t type=d[11];uint32_t len=type==0?12:16;
        if(n-off<len||(len==16&&copyin(addr+off,d,16)))break;
        if(type==1||type==3){
            uint64_t ptr=0;uint32_t size=0;memcpy(&ptr,d,8);memcpy(&size,d+12,4);
            b2l_diag_record("B2L IPCOOL: seq=%llu descriptor=%u type=%u bytes=%u captured_max=2048\n",seq,i,type,size);
            bytes(seq,"ool",ptr,size<2048?size:2048);
            break; /* at most one OOL region per message */
        }
        if(type!=0&&type!=2&&type!=4)break;
        off+=len;
    }
}
void b2l_diag_syscall(unsigned code,int error,const uint64_t *args)
{
    int r=role();if(r<0||!error||!args)return;
    if(__atomic_fetch_add(&sys_samples[r],1,__ATOMIC_RELAXED)>=128)return;
    b2l_diag_record("B2L SYSERR: pid=%d tid=%llu syscall=%u error=%d a0=0x%llx a1=0x%llx\n",
        proc_selfpid(),thread_tid(current_thread()),code,error,args[0],args[1]);
    uint64_t path=0;
    switch(code){
    case SYS_open:case SYS_stat64:case SYS_lstat64:case SYS_access:
    case SYS_mkdir:case SYS_getattrlist:case SYS_rename:path=args[0];break;
    case SYS_openat:case SYS_mkdirat:case SYS_fstatat64:case SYS_renameat:
    case SYS_renameatx_np:path=args[1];break;
    default:break;
    }
    char text[256];size_t len=0;
    if(path&&!copyinstr(path,text,sizeof(text),&len)&&len){
        for(size_t i=0;i+1<len;i++)if((unsigned char)text[i]<32||(unsigned char)text[i]>126)text[i]='?';
        b2l_diag_record("B2L SYSPATH: pid=%d tid=%llu syscall=%u error=%d path=%s\n",proc_selfpid(),thread_tid(current_thread()),code,error,text);
    }
}

void b2l_diag_service(const char *className,const char *matchName,uint64_t timeout)
{
    int r=role();if(r<0||r>1)return;
    static unsigned samples[2];
    if(__atomic_fetch_add(&samples[r],1,__ATOMIC_RELAXED)>=64)return;
    b2l_diag_record("B2L IOKITWAIT: pid=%d tid=%llu class=%.96s name=%.96s timeout_ns=%llu\n",
        proc_selfpid(),thread_tid(current_thread()),className?className:"-",matchName?matchName:"-",timeout);
    b2l_diag_wait("iokit-wait",timeout,0,0);
}
