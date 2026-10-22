/* SPDX-License-Identifier: BSD-2-Clause
 * Pins and HSI2C transfer sequence from the user's UEFI_beyond2lte sources.
 * GPIO is read-only. Touch does not update firmware, voltages or power-cycle.
 */
#include <IOKit/IOService.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <kern/clock.h>
#include <arm64/b2l_early.h>
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
    unsigned debounce[4],failures,keyLogs;
    B2L_SEC_TS_POINTER pointer;
    static void poll(OSObject*,IOTimerEventSource*);
    uint32_t r(unsigned off){uint32_t v=*(volatile uint32_t*)(i2c+off);__asm__ volatile("dmb oshld":::"memory");return v;}
    void w(unsigned off,uint32_t v){*(volatile uint32_t*)(i2c+off)=v;__asm__ volatile("dsb oshst":::"memory");}
    void quiesce(){w(0x20,0);w(0x24,r(0x24));w(0x44,r(0x44)&~(1u<<31));w(0,r(0)&~0xc0u);}
    bool message(uint8_t *buf,unsigned n,bool read){
        if(!buf||!n||n>256)return false;
        w(0x48,r(0x48)&~(1u<<31));
        unsigned level=n<8?n:8;
        w(4,3u|(level<<4)|(level<<16));
        w(0,(r(0)&~0xc0u)|8u|(read?0x40u:0x80u));
        w(0x40,r(0x40)|(1u<<31));
        w(0x70,(r(0x70)&~0xff0fffffu)|(0x48u<<10));
        w(0x24,r(0x24));w(0x20,0x80);
        w(0x44,(r(0x44)&~0x8003ffffu)|0x80020000u|(read?0x10000u:0)|n);
        unsigned at=0;
        for(unsigned t=0;t<5000;++t){
            uint32_t fifo=r(0x30);
            if(at<n){
                if(read&&!(fifo&(1u<<24)))buf[at++]=(uint8_t)r(0x38);
                else if(!read&&(fifo&0x7f)<16)w(0x34,buf[at++]);
            }
            if(at==n&&(r(0x24)&0x80)&&(read||(r(0x30)&0x100))){quiesce();return true;}
            IODelay(1);
        }
        w(0,r(0)|(1u<<31));w(0,r(0)&~(1u<<31));quiesce();return false;
    }
    bool command(uint8_t cmd,uint8_t *data,unsigned n){return message(&cmd,1,false)&&message(data,n,true);}
public:
    bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
    void stop(IOService *provider) APPLE_KEXT_OVERRIDE;
    void free() APPLE_KEXT_OVERRIDE;
};
OSDefineMetaClassAndStructors(B2LInput,IOService);
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
        uint8_t boot=0;
        /* Timing was initialized by firmware, never guess clock dividers. */
        if(r(0x60)&&r(0x64)&&command(0x55,&boot,1)&&boot==0x20)touch=true;
        quiesce();
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
    if(s->touch){
        bool ok=true;
        for(unsigned i=0;i<4;++i){
            uint8_t event[8]={0};
            if(!s->command(0x60,event,sizeof(event))){ok=false;break;}
            if(!event[0])break;
            if(B2LSecTsParseEvent(event,&s->pointer)==B2LSecTsUpdated)
                b2l_hid_touch(0,s->pointer.Active,s->pointer.X,s->pointer.Y);
            if(!B2LSecTsRemainingEvents(event))break;
        }
        if(ok)s->failures=0;
        else if(++s->failures>=3){
            s->touch=false;s->quiesce();
            b2l_hid_touch(0,0,s->pointer.X,s->pointer.Y);
            IOLog("B2L INPUT: sec_ts disabled after 3 transfer failures; GPIO remains active\n");
        }
    }
    sender->setTimeoutMS(20);
}
void B2LInput::stop(IOService *p){if(timer)timer->cancelTimeout();if(i2c)quiesce();IOService::stop(p);}
void B2LInput::free(){
    if(timer&&loop)loop->removeEventSource(timer);OSSafeReleaseNULL(timer);OSSafeReleaseNULL(loop);
    OSSafeReleaseNULL(gpioMap);OSSafeReleaseNULL(i2cMap);OSSafeReleaseNULL(gpioDesc);OSSafeReleaseNULL(i2cDesc);IOService::free();
}
