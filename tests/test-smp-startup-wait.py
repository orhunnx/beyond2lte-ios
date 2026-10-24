#!/usr/bin/env python3
"""Exercise the actual per-core startup wait before the next CPU_ON."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[1];s=(r/'xnu-overlay/iokit/Kernel/arm/B2LCPU.cpp').read_text();a=s.index('void B2LCPU::waitForStartup(');b=s.index('void B2LCPU::finishStartup(',a)
pre=r'''
#include <cassert>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <cstdarg>
#include <cstdio>
#include "b2l_secondary.h"
constexpr uint64_t NSEC_PER_SEC=1000000000,B2L_COUNTER_HZ=1000;
constexpr int TRUE=1;

static uint64_t data[19];static volatile uint64_t*secondary_page=data;
class B2LCPU{public:unsigned cpuInitialized=0;static void waitForStartup(unsigned);};
static B2LCPU cpu;static B2LCPU*b2l_cpus[8];
static unsigned sleeps,readyAfter;static uint64_t now;static bool reject,ipi,timer;
static unsigned logged;static std::string lastPanic;
static void nanoseconds_to_deadline(uint64_t ns,uint64_t*out){*out=now+ns/1000000;}
static uint64_t mach_absolute_time(){return now;}
static uint64_t b2l_smp_progress(unsigned id){assert(id==6);return cpu.cpuInitialized?18:0;}
static bool b2l_smp_rejected(){return reject;}
static bool b2l_cpu_ipi_ready(unsigned id){assert(id==6);return ipi&&cpu.cpuInitialized;}
static uint64_t b2l_cpu_irq_count(unsigned id,int t){assert(id==6&&t==TRUE);return timer&&cpu.cpuInitialized;}
static void b2l_early_trace(const char*,...){++logged;}
[[noreturn]]static void panic(const char*m,...){char buf[2048];va_list a;va_start(a,m);vsnprintf(buf,sizeof(buf),m,a);va_end(a);lastPanic=buf;throw std::runtime_error(buf);}
static void IOSleep(unsigned ms){now+=ms;++sleeps;if(sleeps==readyAfter)cpu.cpuInitialized=1;}
static void reset(){for(auto&p:b2l_cpus)p=&cpu;cpu.cpuInitialized=0;sleeps=logged=0;readyAfter=5;now=1;reject=false;ipi=timer=true;lastPanic.clear();data[5]=0x908513000ULL;data[6]=2;data[16]=4;data[17]=1;data[18]=0x531f0030;}
'''
post=r'''
int main(){
 reset();B2LCPU::waitForStartup(6);assert(sleeps==5&&logged>=2&&lastPanic.empty());
 reset();cpu.cpuInitialized=1;B2LCPU::waitForStartup(6);assert(!sleeps);
 reset();reject=true;try{B2LCPU::waitForStartup(6);assert(false);}catch(const std::runtime_error&){}
 assert(!sleeps&&lastPanic.find("rejected")!=std::string::npos);
 reset();timer=false;B2LCPU::waitForStartup(6);assert(sleeps==5 && lastPanic.empty());
 for(unsigned scenario=0;scenario<2;scenario++){
  reset();if(scenario==0)readyAfter=10000;if(scenario==1)ipi=false;
  try{B2LCPU::waitForStartup(6);assert(false);}catch(const std::runtime_error&){}
  assert(now>5000&&sleeps<=5001&&lastPanic.find("incomplete")!=std::string::npos);
  assert(lastPanic.find("B2L LOW: EL=1 MPIDR=0x1 MIDR=0x531f0030 target=0x908513000")!=std::string::npos);
 }
}
'''
with tempfile.TemporaryDirectory(prefix='b2l-startup-wait-') as td:
 p=Path(td);(p/'test.cpp').write_text(pre+s[a:b]+post)
 subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(r/'common/smp'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('actual startup wait: ready/late CPU, affinity rejection, idle CPU without timer, missing C/SGI and finite deadline PASS')
