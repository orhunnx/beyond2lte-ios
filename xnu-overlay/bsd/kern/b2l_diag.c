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
static int progress_role(void)
{
    proc_t p=current_proc();if(!p||!proc_is64bit(p))return -1;
    int r=-1;
    if(!strcmp(p->p_comm,"SpringBoard"))r=0;
    else if(!strcmp(p->p_comm,"backboardd"))r=1;
    else if(!strncmp(p->p_comm,"containermanage",14))r=2;
    else if(!strcmp(p->p_comm,"lsd"))r=3;
    else if(!strcmp(p->p_comm,"assetsd"))r=4;
    else if(!strcmp(p->p_comm,"tccd"))r=5;
    if(r<0)return -1;
    int requested=0;PE_parse_boot_argn("b2l_diag",&requested,sizeof(requested));
    return requested==1?r:-1;
}
static int role(void)
{
    int r=progress_role();return r<4?r:-1;
}
/* Resident last-observation cache, independent of payload/backtrace quotas.
 * Only the current thread writes its own slot. All shared fields are atomic;
 * the worker makes bounded version-checked copies, never remote stack reads,
 * process lookups, thread suspension, allocation, or waits on an IPC lock.
 * An old observation is not proof the thread is alive or still in that call. */
#define B2L_PROGRESS_ROLES 6u
#define B2L_PROGRESS_SLOTS 16u
struct progress_slot {uint64_t tid,version,sec,pid,event,a,b,c,pc;int error;};
static struct progress_slot progress_slots[B2L_PROGRESS_ROLES][B2L_PROGRESS_SLOTS];
static uint64_t progress_overflow;
static uint64_t progress_seconds(void)
{
    uint64_t ns=0;absolutetime_to_nanoseconds(mach_absolute_time(),&ns);return ns/1000000000;
}
static void progress_note(int r,unsigned event,uint64_t a,uint64_t b,uint64_t c,int error)
{
    if(r<0||r>=B2L_PROGRESS_ROLES)return;
    uint64_t tid=thread_tid(current_thread());if(!tid)return;
    for(unsigned i=0;i<B2L_PROGRESS_SLOTS;i++){
        struct progress_slot *p=&progress_slots[r][i];
        uint64_t old=__atomic_load_n(&p->tid,__ATOMIC_RELAXED);
        if(old!=tid&&!(old==0&&__atomic_compare_exchange_n(&p->tid,&old,tid,0,__ATOMIC_RELAXED,__ATOMIC_RELAXED)))continue;
        uint64_t v=__atomic_load_n(&p->version,__ATOMIC_RELAXED);
        if((v&1)||!__atomic_compare_exchange_n(&p->version,&v,v+1,0,__ATOMIC_ACQ_REL,__ATOMIC_RELAXED))return;
        arm_saved_state_t *regs=get_user_regs(current_thread());
        __atomic_store_n(&p->sec,progress_seconds(),__ATOMIC_RELAXED);
        __atomic_store_n(&p->pid,proc_selfpid(),__ATOMIC_RELAXED);
        __atomic_store_n(&p->event,event,__ATOMIC_RELAXED);
        __atomic_store_n(&p->a,a,__ATOMIC_RELAXED);__atomic_store_n(&p->b,b,__ATOMIC_RELAXED);__atomic_store_n(&p->c,c,__ATOMIC_RELAXED);
        __atomic_store_n(&p->pc,regs?(uint64_t)get_saved_state_pc(regs):0,__ATOMIC_RELAXED);
        __atomic_store_n(&p->error,error,__ATOMIC_RELAXED);
        __atomic_store_n(&p->version,v+2,__ATOMIC_RELEASE);return;
    }
    __atomic_fetch_add(&progress_overflow,1,__ATOMIC_RELAXED);
}
void b2l_diag_progress(void)
{
    int requested=0;PE_parse_boot_argn("b2l_diag",&requested,sizeof(requested));if(requested!=1)return;
    static const char *const names[]={"SpringBoard","backboardd","container","lsd","assetsd","tccd"};
    uint64_t now=progress_seconds();unsigned records=0,busy=0;
    for(unsigned r=0;r<B2L_PROGRESS_ROLES;r++)for(unsigned i=0;i<B2L_PROGRESS_SLOTS;i++){
        struct progress_slot *p=&progress_slots[r][i],q={0};bool valid=false;
        for(unsigned attempt=0;attempt<3;attempt++){
            uint64_t v=__atomic_load_n(&p->version,__ATOMIC_ACQUIRE);
            if(!v)break;if(v&1)continue;
#define B2L_PROGRESS_LOAD(field) q.field=__atomic_load_n(&p->field,__ATOMIC_RELAXED)
            B2L_PROGRESS_LOAD(tid);B2L_PROGRESS_LOAD(sec);B2L_PROGRESS_LOAD(pid);B2L_PROGRESS_LOAD(event);
            B2L_PROGRESS_LOAD(a);B2L_PROGRESS_LOAD(b);B2L_PROGRESS_LOAD(c);B2L_PROGRESS_LOAD(pc);B2L_PROGRESS_LOAD(error);
#undef B2L_PROGRESS_LOAD
            __atomic_thread_fence(__ATOMIC_ACQUIRE);
            if(v==__atomic_load_n(&p->version,__ATOMIC_RELAXED)){valid=true;break;}
        }
        if(!valid){if(__atomic_load_n(&p->version,__ATOMIC_RELAXED))busy++;continue;}
        if(!q.tid||!q.pid)continue;
        b2l_diag_record("B2L PROGRESS: sec=%llu role=%s pid=%llu tid=%llu observed=%llu age=%llu event=%llu a=0x%llx b=0x%llx c=0x%llx pc=0x%llx error=%d\n",
            now,names[r],q.pid,q.tid,q.sec,now>=q.sec?now-q.sec:0,q.event,q.a,q.b,q.c,q.pc,q.error);records++;
    }
    b2l_diag_record("B2L PROGRESS SUMMARY: sec=%llu slots=%u busy=%u overflow=%llu\n",now,records,busy,__atomic_load_n(&progress_overflow,__ATOMIC_RELAXED));
}
struct sample_slot {uint64_t tid,last;unsigned count,window;};
static struct sample_slot slots[4][16];
/* Current thread only, before potentially blocking native calls. A thread
 * which never returns still leaves its entry PC/stack in the persistent log. */
void b2l_diag_wait(const char *kind,uint64_t a,uint64_t b,uint64_t c)
{
    int pr=progress_role();
    progress_note(pr,!strcmp(kind,"syscall")?1:2,a,b,c,0);
    int r=pr<4?pr:-1;if(r<0)return;
    uint64_t tid=thread_tid(current_thread()),ns=0;
    absolutetime_to_nanoseconds(mach_absolute_time(),&ns);
    uint64_t sec=ns/1000000000+1;
    struct sample_slot *slot=NULL;
    for(unsigned i=0;i<16;i++){
        uint64_t old=__atomic_load_n(&slots[r][i].tid,__ATOMIC_RELAXED);
        if(old==tid){slot=&slots[r][i];break;}
        if(!old&&__atomic_compare_exchange_n(&slots[r][i].tid,&old,tid,1,__ATOMIC_RELAXED,__ATOMIC_RELAXED)){slot=&slots[r][i];break;}
    }
    if(!slot)return;
    /* Preserve fresh GUI stacks after early startup samples are exhausted. */
    unsigned window=r<2?(sec>=600?3:sec>=300?2:sec>=180?1:0):0;
    if(slot->window!=window){slot->window=window;slot->count=0;slot->last=0;}
    if(slot->count>=8||(slot->last&&sec-slot->last<5))return;
    /* Each tid owns its slot. No remote stack reads or shared sample buffer. */
    slot->last=sec;unsigned sample=++slot->count;int pid=proc_selfpid();
    arm_saved_state_t *regs=get_user_regs(current_thread());
    b2l_diag_record("B2L WAIT: pid=%d tid=%llu sample=%u sec=%llu window=%u kind=%s a=0x%llx b=0x%llx c=0x%llx pc=0x%llx\n",
        pid,tid,sample,sec,window,kind,a,b,c,regs?(uint64_t)get_saved_state_pc(regs):0);
    uintptr_t bt[12];struct backtrace_user_info bi=BTUINFO_INIT;
    unsigned n=backtrace_user(bt,12,NULL,&bi);
    b2l_diag_record("B2L WAITBT: pid=%d tid=%llu sample=%u frames=%u error=%d\n",pid,tid,sample,n,bi.btui_error);
    for(unsigned i=0;i<n&&i<12;i++)b2l_diag_record("B2L WAITBT: pid=%d tid=%llu sample=%u frame=%u pc=0x%llx\n",pid,tid,sample,i,(uint64_t)bt[i]);
}
static unsigned ipc_samples[4][2],ipc_seen[4][2],sys_samples[4];
/* Never recycle slots during a boot: a restarting lsd must not starve MCM,
 * and one daemon's dyld/bootstrap traffic must not exhaust another's limit.
 * Fields/counters are atomic; no locks or allocation in IPC/syscall hooks. */
struct process_sample { unsigned pid, ipc[2], errors, csops, exists; };
static struct process_sample process_samples[2][8];
static struct process_sample *container_sample(int r)
{
    if(r<2||r>3)return NULL;
    unsigned pid=(unsigned)proc_selfpid();
    if(!pid)return NULL;
    for(unsigned i=0;i<8;i++){
        struct process_sample *p=&process_samples[r-2][i];
        unsigned old=__atomic_load_n(&p->pid,__ATOMIC_RELAXED);
        if(old==pid)return p;
        if(!old&&__atomic_compare_exchange_n(&p->pid,&old,pid,0,__ATOMIC_RELAXED,__ATOMIC_RELAXED))return p;
    }
    return NULL;
}

static uint64_t message_sequence;
static unsigned raw_budget=4*1024*1024;
static unsigned raw_reserve_from(unsigned *budget,unsigned n)
{
    unsigned old=__atomic_load_n(budget,__ATOMIC_RELAXED);
    for(unsigned i=0;i<4;i++){
        unsigned take=n<old?n:old;if(!take)return 0;
        if(__atomic_compare_exchange_n(budget,&old,old-take,1,__ATOMIC_RELAXED,__ATOMIC_RELAXED))return take;
    }return 0;
}
static void bytes_from(uint64_t seq,const char *part,uint64_t addr,unsigned n,unsigned *budget)
{
    if(!addr||addr>UINT64_MAX-n)return;
    n=raw_reserve_from(budget,n);
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
static void bytes(uint64_t seq,const char *part,uint64_t addr,unsigned n)
{
    bytes_from(seq,part,addr,n,&raw_budget);
}
/* Separate exact-name TCC windows: early tccd traffic cannot consume the later
 * assetsd request/reply budget. Observe native user buffers only; no IPC changes. */
static unsigned tcc_ipc_samples[2][2],tcc_raw_budget[2]={65536,131072};
static void tcc_ipc(int rx,uint64_t addr,uint32_t n,uint32_t bits,int32_t id,
    uint32_t remote,uint32_t local,uint32_t count)
{
    if(rx<0||rx>1||n<24||!addr||addr>UINT64_MAX-n)return;
    proc_t p=current_proc();if(!p||!proc_is64bit(p))return;
    int slot=!strcmp(p->p_comm,"assetsd")?0:!strcmp(p->p_comm,"tccd")?1:-1;
    if(slot<0)return;
    int requested=0;PE_parse_boot_argn("b2l_diag",&requested,sizeof(requested));
    if(requested!=1)return;
    uint32_t h[7]={0};
    if(rx){
        if(copyin(addr,h,24)||h[1]<24||h[1]>n)return;
        bits=h[0];n=h[1];remote=h[2];local=h[3];id=(int32_t)h[5];
        if((bits&0x80000000u)&&n>=28&&!copyin(addr+24,h,4))count=h[0];
    }
    progress_note(slot+4,rx?5:4,(uint32_t)id,remote,local,0);
    if(__atomic_fetch_add(&tcc_ipc_samples[slot][rx],1,__ATOMIC_RELAXED)>=(slot?256u:128u))return;
    uint64_t seq=__atomic_add_fetch(&message_sequence,1,__ATOMIC_RELAXED);
    b2l_diag_record("B2L IPC: seq=%llu dir=%s role=%d pid=%d tid=%llu id=0x%x bits=0x%x remote=0x%x local=0x%x bytes=%u descriptors=%u\n",
        seq,rx?"rx":"tx",slot+4,proc_selfpid(),thread_tid(current_thread()),id,bits,remote,local,n,count);
    bytes_from(seq,"inline",addr,n<1024?n:1024,&tcc_raw_budget[slot]);
    if(!(bits&0x80000000u)||n<28||count>16)return;
    uint32_t off=28;
    for(unsigned i=0;i<count;i++){
        uint8_t d[16]={0};if(off>n||n-off<12||copyin(addr+off,d,12))break;
        uint8_t type=d[11];uint32_t len=type==0?12:16;
        if(n-off<len||(len==16&&copyin(addr+off,d,16)))break;
        if(type==1||type==3){
            uint64_t ptr=0;uint32_t size=0;memcpy(&ptr,d,8);memcpy(&size,d+12,4);
            b2l_diag_record("B2L IPCOOL: seq=%llu descriptor=%u type=%u bytes=%u captured_max=2048\n",seq,i,type,size);
            bytes_from(seq,"ool",ptr,size<2048?size:2048,&tcc_raw_budget[slot]);break;
        }
        if(type!=0&&type!=2&&type!=4)break;
        off+=len;
    }
}
/* Keep independent late GUI windows. Numeric Mach IDs are not a protocol
 * allowlist; startup chatter must not hide render-server requests and replies. */
static unsigned gui_ipc_samples[2][4][2],gui_port_samples[2][4][2],gui_receive_samples[2][4];
static unsigned gui_bootstrap_ipc[2][2],gui_bootstrap_ports[2][2],gui_bootstrap_receive[2];
static bool gui_bootstrap_id(int32_t id){return id==0x400000cf||id==0x20000000;}
/* Native CARenderServerGetDisplays request/reply IDs. Discovery traffic and
 * unrelated GUI messages cannot exhaust this specific post-lookup evidence. */
static int gui_render_slot(int32_t id){return id==0x9d27?0:id==0x9d8b?1:id==0x47?2:-1;}
static bool gui_render_id(int32_t id){return gui_render_slot(id)>=0;}
/* Each numeric kind has its own quota; unrelated send-once notifications cannot
 * crowd out the display request or reply. id0x47 is evidence, not a failure. */
static unsigned gui_render_ipc[2][3][2],gui_render_ports[2][3][2],gui_render_receive[2][3];
static unsigned gui_render_budget[2]={65536,65536};
static unsigned gui_raw_budget[2]={262144,262144};
static int gui_window(void)
{
    uint64_t ns=0;absolutetime_to_nanoseconds(mach_absolute_time(),&ns);
    uint64_t sec=ns/1000000000;
    return sec<90?-1:sec<180?0:sec<300?1:sec<600?2:3;
}
/* Send arguments describe the current user wire ABI, including vector calls.
 * On receive, header/descriptor count are re-read only after native copyout.
 * Copy failures and truncated payloads are diagnostics, never new IPC errors. */
void b2l_diag_ipc(int rx,uint64_t addr,uint32_t n,uint32_t bits,int32_t id,
    uint32_t remote,uint32_t local,uint32_t count)
{
    tcc_ipc(rx,addr,n,bits,id,remote,local,count);
    int r=role();if(r<0||rx<0||rx>1||n<24||!addr||addr>UINT64_MAX-n)return;
    uint32_t h[7]={0};
    if(rx){
        if(copyin(addr,h,24)||h[1]<24||h[1]>n)return;
        bits=h[0];n=h[1];remote=h[2];local=h[3];id=(int32_t)h[5];
        if((bits&0x80000000u)&&n>=28&&!copyin(addr+24,h,4))count=h[0];
    }
    /* Observed iOS26 CPX@ traffic also uses 0x400000cf/0x20000000.
     * For MCM/lsd retain every numeric ID within a bounded per-process window;
     * do not infer request semantics from an ID or miss errors via an ID filter.
     * GUI admission stays at its existing small discovery/XPC limit. */
    progress_note(r,rx?5:4,(uint32_t)id,remote,local,0);
    int window=r<2?gui_window():-1;
    if(r>=2){
        struct process_sample *p=container_sample(r);if(!p)return;
        unsigned sample=__atomic_fetch_add(&p->ipc[rx],1,__ATOMIC_RELAXED);
        if(sample>=(r==2?512u:256u))return;
    }else if(gui_render_id(id)){
        if(__atomic_fetch_add(&gui_render_ipc[r][gui_render_slot(id)][rx],1,__ATOMIC_RELAXED)>=32)return;
    }else if(gui_bootstrap_id(id)){
        if(__atomic_fetch_add(&gui_bootstrap_ipc[r][rx],1,__ATOMIC_RELAXED)>=32)return;
    }else if(window>=0){
        if(__atomic_fetch_add(&gui_ipc_samples[r][window][rx],1,__ATOMIC_RELAXED)>=32)return;
    }else{
        unsigned seen=__atomic_fetch_add(&ipc_seen[r][rx],1,__ATOMIC_RELAXED);
        if(seen>=24&&id!=0x10000000&&id!=0x10000001)return;
        unsigned sample=__atomic_fetch_add(&ipc_samples[r][rx],1,__ATOMIC_RELAXED);
        if(sample>=128)return;
    }
    uint64_t seq=__atomic_fetch_add(&message_sequence,1,__ATOMIC_RELAXED)+1;
    b2l_diag_record("B2L IPC: seq=%llu dir=%s role=%d pid=%d tid=%llu id=0x%x bits=0x%x remote=0x%x local=0x%x bytes=%u descriptors=%u\n",
        seq,rx?"rx":"tx",r,proc_selfpid(),thread_tid(current_thread()),id,bits,remote,local,n,count);
    unsigned inline_max=r>=2?3072u:1024u;
    unsigned *budget=r<2?(gui_render_id(id)?&gui_render_budget[r]:&gui_raw_budget[r]):&raw_budget;
    bytes_from(seq,"inline",addr,n<inline_max?n:inline_max,budget);
    if(!(bits&0x80000000u)||n<28||count>16)return;
    uint32_t off=28;
    for(unsigned i=0;i<count;i++){
        uint8_t d[16]={0};if(off>n||n-off<12||copyin(addr+off,d,12))break;
        uint8_t type=d[11];uint32_t len=type==0?12:16;
        if(n-off<len||(len==16&&copyin(addr+off,d,16)))break;
        if(type==1||type==3){
            uint64_t ptr=0;uint32_t size=0;memcpy(&ptr,d,8);memcpy(&size,d+12,4);
            b2l_diag_record("B2L IPCOOL: seq=%llu descriptor=%u type=%u bytes=%u captured_max=2048\n",seq,i,type,size);
            bytes_from(seq,"ool",ptr,size<2048?size:2048,budget);
            break; /* at most one OOL region per message */
        }
        if(type!=0&&type!=2&&type!=4)break;
        off+=len;
    }
}
/* Native delivery location, not the original vector base. Trailer audit
 * token is copied only from bytes which native IPC already returned. */
void b2l_diag_receive(uint64_t addr,uint32_t n,uint32_t trailer,
    uint64_t base,uint32_t capacity,uint64_t options)
{
    b2l_diag_ipc(1,addr,n,0,0,0,0,0);
    int r=role();if(r<0)return;
    static unsigned samples[2];
    if(r<2){
        uint32_t header[6];if(n<24||copyin(addr,header,sizeof(header)))return;
        if(gui_render_id((int32_t)header[5])){
            if(__atomic_fetch_add(&gui_render_receive[r][gui_render_slot((int32_t)header[5])],1,__ATOMIC_RELAXED)>=32)return;
        }else if(gui_bootstrap_id((int32_t)header[5])){
            if(__atomic_fetch_add(&gui_bootstrap_receive[r],1,__ATOMIC_RELAXED)>=32)return;
        }else{
            int w=gui_window();if(w<0||__atomic_fetch_add(&gui_receive_samples[r][w],1,__ATOMIC_RELAXED)>=32)return;
        }
    }else if(__atomic_fetch_add(&samples[r-2],1,__ATOMIC_RELAXED)>=1024)return;
    uint32_t h[6]={0},t[13]={0};
    if(n<24||addr>UINT64_MAX-n||copyin(addr,h,sizeof(h)))return;
    unsigned take=trailer<sizeof(t)?trailer:sizeof(t);int error=-1;
    if(take>=8&&addr+n<=UINT64_MAX-take)error=copyin(addr+n,t,take);
    b2l_diag_record("B2L IPCRCV: pid=%d tid=%llu id=0x%x local=0x%x remote=0x%x actual=0x%llx base=0x%llx capacity=%u options=0x%llx trailer=%u copy=%d type=%u size=%u audit=%u,%u,%u,%u,%u,%u,%u,%u\n",
        proc_selfpid(),thread_tid(current_thread()),h[5],h[3],h[2],addr,base,capacity,options,
        trailer,error,t[0],t[1],t[5],t[6],t[7],t[8],t[9],t[10],t[11],t[12]);
}
/* Port tokens are permuted address snapshots, never owned references and
 * never dereferenced. Address reuse can alias tokens: correlate PID/name/time.
 * Track initial lsd reply endpoints without allocating in native IPC paths. */
static uint64_t reply_tokens[128];
static unsigned port_samples[4][2],sonce_samples;
static uint64_t sonce_sequence;
struct pending_sonce {uint64_t tid,event;};
static struct pending_sonce pending_sonce[64];
static unsigned pending_sonce_count;
void b2l_diag_ports(int rx,int32_t id,uint32_t bits,uint64_t remote,
    uint64_t local,uint32_t user_remote,uint32_t user_local)
{
    int r=role();if(r<0||rx<0||rx>1)return;
    if(r<2){
        if(gui_render_id(id)){
            if(__atomic_fetch_add(&gui_render_ports[r][gui_render_slot(id)][rx],1,__ATOMIC_RELAXED)>=32)return;
        }else if(gui_bootstrap_id(id)){
            if(__atomic_fetch_add(&gui_bootstrap_ports[r][rx],1,__ATOMIC_RELAXED)>=32)return;
        }else{
            int w=gui_window();if(w<0||__atomic_fetch_add(&gui_port_samples[r][w][rx],1,__ATOMIC_RELAXED)>=32)return;
        }
    }
    if(!rx&&r==3&&local&&((bits>>8)&0xff)==18){
        for(unsigned i=0;i<128;i++){
            uint64_t old=__atomic_load_n(&reply_tokens[i],__ATOMIC_RELAXED);
            if(old==local)break;
            if(!old&&__atomic_compare_exchange_n(&reply_tokens[i],&old,local,0,__ATOMIC_RELAXED,__ATOMIC_RELAXED))break;
        }
    }
    if(r>=2&&__atomic_fetch_add(&port_samples[r][rx],1,__ATOMIC_RELAXED)>=512)return;
    b2l_diag_record("B2L IPCPORT: dir=%s pid=%d tid=%llu id=0x%x bits=0x%x dest=0x%llx reply=0x%llx user_dest=0x%x user_reply=0x%x\n",
        rx?"deliver":"send",proc_selfpid(),thread_tid(current_thread()),id,bits,remote,local,user_remote,user_local);
}
void b2l_diag_sonce(uint64_t key,uint32_t receiver,uint64_t context)
{
    int r=role(),tracked=0;
    for(unsigned i=0;i<128;i++)if(key&&__atomic_load_n(&reply_tokens[i],__ATOMIC_RELAXED)==key){tracked=1;break;}
    if(r<2&&!tracked)return;
    if(__atomic_fetch_add(&sonce_samples,1,__ATOMIC_RELAXED)>=128)return;
    uint64_t event=__atomic_fetch_add(&sonce_sequence,1,__ATOMIC_RELAXED)+1;
    uint64_t tid=thread_tid(current_thread());
    arm_saved_state_t *regs=get_user_regs(current_thread());
    b2l_diag_record("B2L SONCE: event=%llu pid=%d tid=%llu role=%d token=0x%llx receiver=0x%x context=0x%llx tracked=%d pc=0x%llx\n",
        event,proc_selfpid(),tid,r,key,receiver,context,tracked,regs?(uint64_t)get_saved_state_pc(regs):0);
    uintptr_t bt[12];unsigned n=backtrace(bt,12,NULL,NULL);
    for(unsigned i=0;i<n;i++)b2l_diag_record("B2L SONCEKBT: event=%llu frame=%u pc=0x%llx\n",event,i,(uint64_t)bt[i]);
    /* Queue only an event number. Copying user memory is deferred until the
     * current native trap/syscall returns; never fault while an IPC lock is held. */
    for(unsigned i=0;i<64;i++){
        struct pending_sonce *p=&pending_sonce[i];
        uint64_t old=__atomic_load_n(&p->tid,__ATOMIC_RELAXED);
        if(old!=tid&&!(old==0&&__atomic_compare_exchange_n(&p->tid,&old,tid,0,__ATOMIC_RELAXED,__ATOMIC_RELAXED)))continue;
        if(!__atomic_exchange_n(&p->event,event,__ATOMIC_RELEASE))
            __atomic_fetch_add(&pending_sonce_count,1,__ATOMIC_RELEASE);
        break;
    }
}
void b2l_diag_checkpoint(void)
{
    if(!__atomic_load_n(&pending_sonce_count,__ATOMIC_ACQUIRE))return;
    uint64_t tid=thread_tid(current_thread());
    for(unsigned i=0;i<64;i++){
        struct pending_sonce *p=&pending_sonce[i];
        if(__atomic_load_n(&p->tid,__ATOMIC_RELAXED)!=tid)continue;
        uint64_t event=__atomic_exchange_n(&p->event,0,__ATOMIC_ACQUIRE);
        if(!event)return;
        __atomic_fetch_sub(&pending_sonce_count,1,__ATOMIC_RELAXED);
        uintptr_t bt[16];struct backtrace_user_info bi=BTUINFO_INIT;
        unsigned n=backtrace_user(bt,16,NULL,&bi);
        b2l_diag_record("B2L SONCEUBT: event=%llu pid=%d tid=%llu frames=%u error=%d\n",event,proc_selfpid(),tid,n,bi.btui_error);
        for(unsigned j=0;j<n;j++)b2l_diag_record("B2L SONCEUBT: event=%llu frame=%u pc=0x%llx\n",event,j,(uint64_t)bt[j]);
        return;
    }
}
/* Capture only bounded launch-constraint inputs/results, after native return.
 * Diagnostics never alter the syscall result or claim a constraint match. */
static unsigned amfi_samples;
static void amfi_constraint(unsigned code,int error,const uint64_t *args,int r)
{
    if(r!=1||code!=SYS___mac_syscall||args[1]!=0x66)return;
    char policy[8];size_t n=0;
    if(copyinstr(args[0],policy,sizeof(policy),&n)||n!=5||strcmp(policy,"AMFI"))return;
    if(__atomic_fetch_add(&amfi_samples,1,__ATOMIC_RELAXED)>=8)return;
    uint64_t wire[8]={0};int e=copyin(args[2],wire,sizeof(wire));
    uint32_t result=UINT32_MAX,cb=(uint32_t)wire[5],rb=(uint32_t)wire[7];int rc=-1;
    if(!e&&rb>=4)rc=copyin(wire[6],&result,sizeof(result));
    uint64_t seq=__atomic_add_fetch(&message_sequence,1,__ATOMIC_RELAXED);
    b2l_diag_record("B2L AMFI: seq=%llu pid=%d tid=%llu call=0x66 error=%d args_copy=%d constraint_bytes=%u result_bytes=%u result_copy=%d result_code=%u\n",
        seq,proc_selfpid(),thread_tid(current_thread()),error,e,cb,rb,rc,result);
    if(e)return;
    bytes(seq,"amfi_args",args[2],sizeof(wire));
    if(cb&&cb<=16384)bytes(seq,"amfi_constraint",wire[4],cb<512?cb:512);
}
/* TCC clients use native SecTask DER/identity reads. Observe only the result
 * boundary, without adding these names to the broader IPC/payload capture. */
static unsigned tcc_samples[2];
static void tcc_result(unsigned code,int error,const uint64_t *args)
{
    if(!args||(code!=SYS_csops&&code!=SYS_csops_audittoken&&
       (code!=SYS___mac_syscall||!error)))return;
    proc_t p=current_proc();if(!p||!proc_is64bit(p))return;
    int slot=!strcmp(p->p_comm,"assetsd")?0:!strcmp(p->p_comm,"tccd")?1:-1;
    if(slot<0)return;
    int requested=0;PE_parse_boot_argn("b2l_diag",&requested,sizeof(requested));
    if(requested!=1)return;
    if(code==SYS___mac_syscall){
        char policy[8];size_t n=0;
        if(copyinstr(args[0],policy,sizeof(policy),&n)||n!=5||strcmp(policy,"AMFI"))return;
        if(__atomic_fetch_add(&tcc_samples[slot],1,__ATOMIC_RELAXED)>=256)return;
        b2l_diag_record("B2L TCCMAC: caller=%s pid=%d tid=%llu policy=AMFI op=%u error=%d\n",
            p->p_comm,proc_selfpid(),thread_tid(current_thread()),(unsigned)args[1],error);
        return;
    }
    if(__atomic_fetch_add(&tcc_samples[slot],1,__ATOMIC_RELAXED)>=256)return;
    uint32_t status=0,magic=0,length=0;int copied=-1;
    if(!error&&args[1]==0&&args[2]&&args[3]>=sizeof(status))
        copied=copyin(args[2],&status,sizeof(status));
    else if((!error||error==34)&&(args[1]==7||args[1]==16)&&args[2]&&args[3]>=8){
        uint8_t h[8];copied=copyin(args[2],h,sizeof(h));
        if(!copied){
            magic=(uint32_t)h[0]<<24|(uint32_t)h[1]<<16|(uint32_t)h[2]<<8|h[3];
            length=(uint32_t)h[4]<<24|(uint32_t)h[5]<<16|(uint32_t)h[6]<<8|h[7];
        }
    }
    b2l_diag_record("B2L TCCCS: caller=%s pid=%d tid=%llu target=%d audit=%u op=%u size=%llu error=%d status=0x%x magic=0x%x length=%u copied=%d\n",
        p->p_comm,proc_selfpid(),thread_tid(current_thread()),(int)args[0],
        code==SYS_csops_audittoken,(unsigned)args[1],args[3],error,status,magic,length,copied);
}
/* Bounded GUI event-loop completions retain native success/error even after
 * unrelated startup path errors exhaust the ordinary syscall quota. */
static struct {uint64_t tid;unsigned window,count;} gui_event_samples[2][16];
static void gui_event_result(int r,unsigned code,int error,const uint64_t *args)
{
    if(r<0||r>1||(code!=SYS_kevent_qos&&code!=SYS_kevent_id))return;
    int w=gui_window();if(w<0)return;
    uint64_t tid=thread_tid(current_thread());
    for(unsigned i=0;i<16;i++){
        uint64_t old=__atomic_load_n(&gui_event_samples[r][i].tid,__ATOMIC_RELAXED);
        if(old!=tid&&!(old==0&&__atomic_compare_exchange_n(&gui_event_samples[r][i].tid,&old,tid,0,__ATOMIC_RELAXED,__ATOMIC_RELAXED)))continue;
        if(gui_event_samples[r][i].window!=(unsigned)w){gui_event_samples[r][i].window=w;gui_event_samples[r][i].count=0;}
        if(gui_event_samples[r][i].count++>=4)return;
        b2l_diag_record("B2L EVRESULT: pid=%d tid=%llu window=%d syscall=%u fd_or_id=0x%llx changes=%u events=%u flags=0x%x error=%d\n",
            proc_selfpid(),tid,w,code,args[0],(unsigned)args[2],(unsigned)args[4],(unsigned)args[7],error);
        return;
    }
}
void b2l_diag_syscall(unsigned code,int error,const uint64_t *args)
{
    b2l_diag_checkpoint();
    progress_note(progress_role(),3,code,args?args[0]:0,args?args[1]:0,error);
    tcc_result(code,error,args);
    int r=role();if(r<0||!args)return;
    gui_event_result(r,code,error,args);
    amfi_constraint(code,error,args,r);
    struct process_sample *p=r>=2?container_sample(r):NULL;
    if(r>=2&&!p)return;
    /* Log successful AND failed native results, including the CDHash cases
     * that return before the old kern_proc.c out-label trace. Original target
     * lookup, audit-token validation and authorization remain unchanged. */
    if(p&&(code==SYS_csops||code==SYS_csops_audittoken)&&
       __atomic_fetch_add(&p->csops,1,__ATOMIC_RELAXED)<256){
        uint32_t status=0;int read_error=-1;
        if(!error&&args[1]==0&&args[2]&&args[3]>=sizeof(status))
            read_error=copyin(args[2],&status,sizeof(status));
        b2l_diag_record("B2L CSRESULT: pid=%d tid=%llu target=%d op=%u size=%llu error=%d status=0x%x status_copy=%d\n",
            proc_selfpid(),thread_tid(current_thread()),(int)args[0],(unsigned)args[1],args[3],error,status,read_error);
    }
    if(!error)return;
    if(p){
        /* Expected mkdir EEXIST probes have a separate small budget. */
        unsigned *counter=(error==17&&(code==SYS_mkdir||code==SYS_mkdirat))?&p->exists:&p->errors;
        if(__atomic_fetch_add(counter,1,__ATOMIC_RELAXED)>=(counter==&p->exists?16u:256u))return;
    }else if(__atomic_fetch_add(&sys_samples[r],1,__ATOMIC_RELAXED)>=128)return;
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
    progress_note(r,6,timeout,0,0,0);
    static unsigned samples[2];
    if(__atomic_fetch_add(&samples[r],1,__ATOMIC_RELAXED)>=64)return;
    b2l_diag_record("B2L IOKITWAIT: pid=%d tid=%llu class=%.96s name=%.96s timeout_ns=%llu\n",
        proc_selfpid(),thread_tid(current_thread()),className?className:"-",matchName?matchName:"-",timeout);
    b2l_diag_wait("iokit-wait",timeout,0,0);
}
