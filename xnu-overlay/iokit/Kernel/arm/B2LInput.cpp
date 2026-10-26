/* SPDX-License-Identifier: BSD-2-Clause
 * Pins and HSI2C transfer sequence from the user's UEFI_beyond2lte sources.
 * GPIO is read-only. Touch does not update firmware, voltages or power-cycle.
 * Implements touch-branch reset-before-transfer / clear / functions / sense-on.
 */
#include <IOKit/IOService.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <kern/clock.h>
#include <arm64/b2l_early.h>
#include <arm64/b2l_diag.h>
#include <pexpert/device_tree.h>
extern "C" {
#include "B2LSecTsParser.h"
}
extern "C" void b2l_hid_key(int,int);
extern "C" void b2l_hid_touch(int,int,int,int);
class B2LInput : public IOService {
    OSDeclareDefaultStructors(B2LInput);
    IOMemoryDescriptor *gpioDesc,*i2cDesc;
    IOMemoryMap *gpioMap,*i2cMap;
    IOWorkLoop *loop;
    IOTimerEventSource *timer;
    volatile uint8_t *gpio,*i2c;
    bool stable[4],candidate[4],touch;
    unsigned debounce[4],failures,keyLogs,transferLogs,timeoutUS;
    unsigned initAttempts;
    bool initPending;
    uint64_t nextInitUS;
    uint64_t pollCount,eventCount,updateCount,bulkCount,transferFailures;
    uint64_t lastPollStart,lastDiagnostic,maxPollGapUS,maxPollWorkUS;
    unsigned maxPending;
    uint64_t transferTimeUS,maxTransferUS;
    static uint64_t uptimeUS(){uint64_t ns=0;absolutetime_to_nanoseconds(mach_absolute_time(),&ns);return ns/1000;}
    uint8_t lastCommand;
    B2L_SEC_TS_POINTER pointer;
    static void poll(OSObject*,IOTimerEventSource*);
    void initializeTouch();
    uint32_t r(unsigned off){uint32_t v=*(volatile uint32_t*)(i2c+off);__asm__ volatile("dmb oshld":::"memory");return v;}
    void w(unsigned off,uint32_t v){*(volatile uint32_t*)(i2c+off)=v;__asm__ volatile("dsb oshst":::"memory");}
    void quiesce(){w(0x20,0);w(0x24,r(0x24));w(0x44,r(0x44)&~(1u<<31));w(0,r(0)&~0xc0u);}
    void resetController(){w(0,r(0)|(1u<<31));w(0,r(0)&~(1u<<31));w(0,8);w(8,0x00ffffff);w(0x40,r(0x40)|(1u<<31));}
    void transferFailure(bool read,unsigned n,unsigned at,unsigned budgetUS,uint64_t elapsedUS){
        if(transferLogs++<12)b2l_diag_record("B2L TOUCH: cmd=0x%x read=%d bytes=%u done=%u timeout_us=%u elapsed_us=%llu ctrl=0x%x int=0x%x err=0x%x trans=0x%x fifo=0x%x addr=0x%x fs1=0x%x fs2=0x%x\n",
            lastCommand,read,n,at,budgetUS,elapsedUS,r(0),r(0x24),r(0x2c),r(0x50),r(0x30),r(0x70),r(0x60),r(0x64));
    }
    void noteTransfer(uint64_t begin){
        uint64_t elapsed=uptimeUS()-begin;
        transferTimeUS+=elapsed;
        if(elapsed>maxTransferUS)maxTransferUS=elapsed;
    }
    bool message(uint8_t *buf,unsigned n,bool read){
        if(!buf||!n||n>256)return false;
        /* A loop count is not a time limit: MMIO, interrupts and scheduling
         * also consume time. Preserve the probe budget and give a 31-record
         * bulk the original 100 ms transport budget; small runtime transfers
         * use 20 ms. Check the monotonic clock even while draining a FIFO. */
        const unsigned budgetUS=(n>8 && timeoutUS<100000)?100000:timeoutUS;
        const uint64_t begin=uptimeUS();
        w(0x48,r(0x48)&~(1u<<31));
        unsigned level=n<8?n:8;
        w(4,3u|(level<<4)|(level<<16));
        w(0,(r(0)&~0xc0u)|8u|(read?0x40u:0x80u));
        w(0x40,r(0x40)|(1u<<31));
        w(0x70,(r(0x70)&~0xff0fffffu)|(0x48u<<10));
        w(0x24,r(0x24));w(0x20,0x80);
        w(0x44,(r(0x44)&~0x8003ffffu)|0x80020000u|(read?0x10000u:0)|n);
        unsigned at=0;
        while(uptimeUS()-begin<budgetUS){
            unsigned before=at;
            while(at<n && uptimeUS()-begin<budgetUS){
                uint32_t fifo=r(0x30);
                if(read){
                    if(fifo&(1u<<24))break;
                    buf[at++]=(uint8_t)r(0x38);
                }else{
                    if((fifo&0x7f)>=16)break;
                    w(0x34,buf[at++]);
                }
            }
            if(uptimeUS()-begin>=budgetUS)break;
            if(at==n && (r(0x24)&0x80) && (read||(r(0x30)&0x100))){
                quiesce();noteTransfer(begin);return true;
            }
            /* Do not stall between bytes already available in the FIFO. */
            if(at==before || at==n)IODelay(1);
        }
        noteTransfer(begin);
        transferFailure(read,n,at,budgetUS,uptimeUS()-begin);
        resetController();quiesce();return false;
    }
    bool writeCommand(uint8_t *data,unsigned n){lastCommand=data[0];resetController();return message(data,n,false);}
    bool command(uint8_t cmd,uint8_t *data,unsigned n){lastCommand=cmd;resetController();return message(&cmd,1,false)&&message(data,n,true);}
public:
    bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
    void stop(IOService *provider) APPLE_KEXT_OVERRIDE;
    void free() APPLE_KEXT_OVERRIDE;
};
OSDefineMetaClassAndStructors(B2LInput,IOService);
void B2LInput::initializeTouch(){
        ++initAttempts;
        timeoutUS=100000;
        uint8_t boot=0,integrity=0,device[5]={0};
        const char *stage="timing";
        /* Reuse firmware timings, never infer dividers or touch PMIC state. */
        if(r(0x60)&&r(0x64)){
            stage="device-id";
            if(command(0x22,device,sizeof(device))){
                stage="integrity";
                if(command(0x21,&integrity,1)){
                    stage="boot-status";
                    if(command(0x55,&boot,1)&&boot==0x20&&(integrity&0x80)){
                        uint8_t clear=0x62,functions[3]={0x30,0x61,0x00},sense=0x10;
                        stage="clear-events";
                        if(writeCommand(&clear,1)){
                            stage="touch-functions";
                            if(writeCommand(functions,3)){
                                stage="sense-on";
                                if(writeCommand(&sense,1)){touch=true;stage="ready";}
                            }
                        }
                    }
                }
            }
        }
        timeoutUS=20000;
        initPending=!touch && initAttempts<4 && r(0x60) && r(0x64);
        nextInitUS=uptimeUS()+1000000;
        b2l_diag_record("B2L TOUCH INIT: stage=%s ready=%d boot=0x%x integrity=0x%x id=%02x%02x%02x%02x%02x attempt=%u retry_pending=%d\n",
            stage,touch,boot,integrity,device[0],device[1],device[2],device[3],device[4],initAttempts,initPending);
        quiesce();
}
bool B2LInput::start(IOService *provider){
    if(!IOService::start(provider))return false;
    gpioDesc=IOMemoryDescriptor::withPhysicalAddress(0x15850000,4096,kIODirectionInOut);
    if(gpioDesc)gpioMap=gpioDesc->map(kIOMapInhibitCache);
    if(!gpioMap)return false;
    gpio=(volatile uint8_t*)gpioMap->getVirtualAddress();
    const unsigned masks[4]={1u<<4,1u<<3,1u<<4,1u<<6};
    for(unsigned k=0;k<4;++k){unsigned off=k?4:0x44;bool down=!( *(volatile uint32_t*)(gpio+off)&masks[k]);stable[k]=candidate[k]=down;}
    i2cDesc=IOMemoryDescriptor::withPhysicalAddress(0x104b0000,4096,kIODirectionInOut);
    if(i2cDesc)i2cMap=i2cDesc->map(kIOMapInhibitCache);
    if(i2cMap){
        i2c=(volatile uint8_t*)i2cMap->getVirtualAddress();
        timeoutUS=100000; /* same bounded probe budget as the touch branch */
        const void *property=NULL;unsigned propertySize=0;DTEntry chosen;
        uint64_t handoff[24]={0};
        if(SecureDTLookupEntry(NULL,"/chosen",&chosen)==kSuccess &&
           SecureDTGetProperty(chosen,"b2l,touch-status",&property,&propertySize)==kSuccess &&
           propertySize==sizeof(handoff))memcpy(handoff,property,sizeof(handoff));
        b2l_diag_record("B2L TOUCH EFI: version=%llu locate=0x%llx init=0x%llx probe=0x%llx reset=0x%llx functions=0x%llx pointer=0x%llx dvdd=0x%llx avdd=0x%llx boot=0x%llx usi=0x%llx transfers=%llu failed=%llu\n",
            handoff[0],handoff[1],handoff[2],handoff[3],handoff[4],handoff[5],handoff[6],handoff[7],handoff[8],handoff[9],handoff[14],handoff[20],handoff[21]);
        b2l_diag_record("B2L TOUCH HANDOFF: fs1=0x%x fs2=0x%x fs3=0x%x sla=0x%x ctrl=0x%x int=0x%llx err=0x%llx trans=0x%llx fifo=0x%llx\n",
            r(0x60),r(0x64),r(0x68),r(0x6c),r(0),handoff[16],handoff[17],handoff[18],handoff[19]);
        /* A preempted early probe must not disable touch for the entire
         * boot. Retry on this driver's workloop, at most four attempts total. */
        initializeTouch();
    }
    loop=IOWorkLoop::workLoop();if(!loop)return false;
    timer=IOTimerEventSource::timerEventSource(this,&B2LInput::poll);
    if(!timer||loop->addEventSource(timer)!=kIOReturnSuccess)return false;
    timer->setTimeoutMS(20);registerService();
    IOLog("B2L INPUT: GPIO GPA0.3/4/6 GPA2.4; active-low; Bixby=Home; sec_ts=%s\n",touch?"ready":"unavailable");
    return true;
}
void B2LInput::poll(OSObject *owner,IOTimerEventSource *sender){
    B2LInput *s=(B2LInput*)owner;
    uint64_t begin=uptimeUS();
    if(s->lastPollStart && begin-s->lastPollStart>s->maxPollGapUS)
        s->maxPollGapUS=begin-s->lastPollStart;
    s->lastPollStart=begin;++s->pollCount;
    const unsigned masks[4]={1u<<4,1u<<3,1u<<4,1u<<6};
    for(unsigned k=0;k<4;++k){
        unsigned off=k?4:0x44;
        bool down=!( *(volatile uint32_t*)(s->gpio+off)&masks[k]);
        if(down!=s->candidate[k]){s->candidate[k]=down;s->debounce[k]=1;}
        else if(s->debounce[k]<2)++s->debounce[k];
        if(s->debounce[k]==2&&down!=s->stable[k]){
            s->stable[k]=down;b2l_hid_key((int)k,down);
            if(s->keyLogs++<32)IOLog("B2L INPUT: key=%u down=%d\n",k,down);
        }
    }
    if(s->initPending && begin>=s->nextInitUS)s->initializeTouch();
    if(s->touch){
        /* Same bounded READ_ONE / READ_ALL sequence as B2LTouchDxe. Read
         * up to 32 FIFO records per tick instead of four individual commands.
         * Deliver the first event before the bulk read so a failed bulk read
         * cannot silently swallow a valid press/release already consumed. */
        uint8_t events[32][B2L_SEC_TS_EVENT_SIZE]={{0}};
        bool ok=s->command(0x60,events[0],B2L_SEC_TS_EVENT_SIZE);
        if(ok && events[0][0]){
            ++s->eventCount;
            if(B2LSecTsParseEvent(events[0],&s->pointer)==B2LSecTsUpdated){
                ++s->updateCount;
                b2l_hid_touch(0,s->pointer.Active,s->pointer.X,s->pointer.Y);
            }
            unsigned remaining=B2LSecTsRemainingEvents(events[0]);
            if(remaining>s->maxPending)s->maxPending=remaining;
            unsigned count=remaining<31?remaining:31;
            if(count){
                ++s->bulkCount;
                ok=s->command(0x61,&events[1][0],count*B2L_SEC_TS_EVENT_SIZE);
                if(ok)for(unsigned i=1;i<=count;++i){
                    ++s->eventCount;
                    if(B2LSecTsParseEvent(events[i],&s->pointer)==B2LSecTsUpdated){
                        ++s->updateCount;
                        b2l_hid_touch(0,s->pointer.Active,s->pointer.X,s->pointer.Y);
                    }
                }
            }
        }
        if(!ok)++s->transferFailures;
        if(ok)s->failures=0;
        else if(++s->failures>=3){
            s->touch=false;s->quiesce();
            b2l_hid_touch(0,0,s->pointer.X,s->pointer.Y);
            IOLog("B2L INPUT: sec_ts disabled after 3 transfer failures; GPIO remains active\n");
        }
    }
    uint64_t end=uptimeUS(),work=end-begin;
    if(work>s->maxPollWorkUS)s->maxPollWorkUS=work;
    if(end-s->lastDiagnostic>=10000000){
        s->lastDiagnostic=end;
        b2l_diag_record("B2L INPUT PERF: sec=%llu polls=%llu wire=%llu updates=%llu bulk=%llu max_pending=%u transfer_failures=%llu max_gap_us=%llu max_work_us=%llu max_transfer_us=%llu bus_us=%llu enabled=%d init_attempts=%u init_pending=%d\n",
            end/1000000,s->pollCount,s->eventCount,s->updateCount,s->bulkCount,
            s->maxPending,s->transferFailures,s->maxPollGapUS,s->maxPollWorkUS,
            s->maxTransferUS,s->transferTimeUS,s->touch,s->initAttempts,s->initPending);
        s->maxPending=0;s->maxPollGapUS=0;s->maxPollWorkUS=0;s->maxTransferUS=0;
    }
    sender->setTimeoutMS(20);
}
void B2LInput::stop(IOService *p){if(timer)timer->cancelTimeout();if(i2c)quiesce();IOService::stop(p);}
void B2LInput::free(){
    if(timer&&loop)loop->removeEventSource(timer);OSSafeReleaseNULL(timer);OSSafeReleaseNULL(loop);
    OSSafeReleaseNULL(gpioMap);OSSafeReleaseNULL(i2cMap);OSSafeReleaseNULL(gpioDesc);OSSafeReleaseNULL(i2cDesc);IOService::free();
}
