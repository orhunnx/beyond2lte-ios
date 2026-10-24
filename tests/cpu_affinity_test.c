/* SPDX-License-Identifier: BSD-2-Clause */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "b2l_cpu_affinity.h"
int main(void){
 const uint64_t midrs[]={0x411fd050,0x412fd0a1,0x531f0030,0x000f0030,0x531fd050};
 for(unsigned hwid=0;hwid<65536;++hwid)for(unsigned type=0;type<5;++type){
  unsigned core=hwid>>8;
  int expected=-1;
  if(type==0 && !(hwid&255) && core<4)expected=core;
  if(type==1 && !(hwid&255) && core>=4 && core<6)expected=core;
  if(type==2 && hwid>=0x100 && hwid<=0x101)expected=6+hwid-0x100;
  assert(b2l_cpu_from_identity(hwid,midrs[type])==expected);
  assert(b2l_cpu_from_identity(UINT64_C(0x81000000)|hwid,midrs[type])==expected);
  assert(b2l_cpu_from_identity(hwid|(1ULL<<16),midrs[type])==-1);
  assert(b2l_cpu_from_identity(hwid|(1ULL<<32),midrs[type])==-1);
 }
 /* The measured affinity collision must choose two different CPU-data slots. */
 assert(b2l_cpu_from_identity(0x80000100,0x411fd050)==1);
 assert(b2l_cpu_from_identity(0x80000100,0x531f0030)==6);
 assert(b2l_cpu_from_identity(0x80000001,0x531f0030)==-1);
 for(unsigned cpu=0;cpu<8;++cpu){
  unsigned group=cpu<4?0:cpu<6?1:2;
  for(unsigned type=0;type<3;++type)assert(b2l_cpu_midr_matches(cpu,midrs[type])==(group==type));
  unsigned fw=cpu<6?cpu:0x100+cpu-6,el1=cpu<6?cpu<<8:fw;
  assert(b2l_cpu_firmware_id(cpu)==(int)fw);
  assert(b2l_cpu_from_identity(el1,midrs[group])==(int)cpu);
  assert(!b2l_cpu_midr_matches(cpu,midrs[group]^(1ULL<<24)));
 }
 assert(!b2l_cpu_midr_matches(8,0x531f0030));assert(b2l_cpu_firmware_id(8)==-1);
 puts("65536 affinities x5 MIDRs, flags/upper fields, measured A55/M4 collision and firmware-ID roundtrips PASS");
}
