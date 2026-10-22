#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Run the actual provider/coordinator bodies with deferred host scheduling.

This checks ordering only; it does not emulate PSCI, XNU locks or hardware SMP.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
s = (root/'xnu-overlay/iokit/Kernel/arm/B2LCPU.cpp').read_text()
a = s.index('    /* IOService matching is asynchronous.')
b = s.index('\nvoid B2LCPU::registerProcessor', a)
publish = s[a:b]  # includes the start() return and closing brace
start = s.index('void B2LCPU::finishStartup(void *, wait_result_t)\n{')
a = s.index('{', start)+1
b = s.index('    uint64_t deadline, last_stage', a)
coordinator = s[a:b]
pre = r'''
#include <algorithm>
#include <cassert>
#include <stdexcept>
#include <vector>
using wait_result_t=int;
using kern_return_t=int;
using thread_t=int;
constexpr int KERN_SUCCESS=0;
class B2LCPU;
static B2LCPU *b2l_cpus[6];
static unsigned providers_ready, registered_cpus, launched, announced;
static unsigned current_cpu;
static bool fail_launch;
static std::vector<unsigned> order;
static void (*scheduled)(void *, wait_result_t);
static void b2l_early_trace(const char *, ...) {}
[[noreturn]] static void panic(const char *, ...) { throw std::runtime_error("panic"); }
static unsigned cpu_number() { return current_cpu; }
static void ml_set_max_cpus(unsigned n) { assert(n==6 && order.empty()); ++announced; }
static void thread_deallocate(thread_t) {}
static void prepareSecondaryPage() { assert(announced==1 && order.empty()); }
static int kernel_thread_start(void(*fn)(void*,wait_result_t),void*,thread_t *out) {
 if(fail_launch) return 5;
 assert(!scheduled);scheduled=fn;*out=1;++launched;return 0;
}
class B2LCPU {
 unsigned id;
public:
 explicit B2LCPU(unsigned i):id(i){}
 bool publish(unsigned id);
 void registerProcessor() { assert(announced==1);order.push_back(id);++registered_cpus; }
 static void finishStartup(void*,wait_result_t);
};
bool B2LCPU::publish(unsigned id) {
'''
post = r'''
int main() {
 unsigned perm[6]={0,1,2,3,4,5};
 B2LCPU objects[6]={B2LCPU(0),B2LCPU(1),B2LCPU(2),B2LCPU(3),B2LCPU(4),B2LCPU(5)};
 unsigned tested=0;
 do {
  std::fill_n(b2l_cpus,6,nullptr);providers_ready=registered_cpus=launched=announced=0;
  order.clear();scheduled=nullptr;current_cpu=0;
  for(unsigned i=0;i<6;++i) {
   assert(objects[perm[i]].publish(perm[i]));
   assert(order.empty() && !registered_cpus);
   assert(launched==(i==5?1U:0U));
  }
  assert(scheduled);scheduled(nullptr,0);
  assert(registered_cpus==6 && (order==std::vector<unsigned>{0,1,2,3,4,5}));
  unsigned before=providers_ready;
  try {objects[0].publish(0);assert(false);}catch(const std::runtime_error&){}
  assert(providers_ready==before && launched==1);
  ++tested;
 } while(std::next_permutation(perm,perm+6));
 assert(tested==720);
 // Reject incomplete topology and a coordinator running on an unexpected CPU.
 providers_ready=5;order.clear();
 try {B2LCPU::finishStartup(nullptr,0);assert(false);}catch(const std::runtime_error&){}
 assert(order.empty());providers_ready=6;current_cpu=1;
 try {B2LCPU::finishStartup(nullptr,0);assert(false);}catch(const std::runtime_error&){}
 assert(order.empty());
 // Scheduling failure must not silently claim CPU registration completion.
 std::fill_n(b2l_cpus,6,nullptr);providers_ready=registered_cpus=launched=0;
 scheduled=nullptr;fail_launch=true;current_cpu=0;
 for(unsigned i=0;i<5;++i)assert(objects[i].publish(i));
 try {objects[5].publish(5);assert(false);}catch(const std::runtime_error&){}
 assert(!scheduled && !launched && !registered_cpus);
}
'''
with tempfile.TemporaryDirectory(prefix='b2l-smp-order-') as td:
    p = Path(td)
    (p/'test.cpp').write_text(pre+publish+'\nvoid B2LCPU::finishStartup(void*,wait_result_t) {\n'+coordinator+'}\n'+post)
    subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror',
                    '-fsanitize=address,undefined',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
print('Actual SMP coordinator: 720 arrival orders, duplicates, bad topology/CPU and launch failure passed')
