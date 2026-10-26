#!/usr/bin/env python3
"""Feed the real EFI tree through the real XNU topology parser and Edge seed policy."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1];src=root/'out/build-env-0063/src/xnu'
s=(src/'osfmk/arm64/machine_routines.c').read_text();parser=s[s.index('void\nml_parse_cpu_topology(void)'):s.index('\nconst ml_topology_info_t *\nml_get_topology_info(void)')]
parser=parser.replace('__builtin_arm_wsr64(', 'write_register(')
s=(src/'osfmk/arm/cpu_topology.h').read_text();structs=s[s.index('typedef struct ml_topology_cpu {'):s.index('} ml_topology_info_t;')+len('} ml_topology_info_t;')]
s=(src/'osfmk/kern/sched_clutch.c').read_text();a=s.index('static pset_id_t\nb2l_sched_initial_preferred_pset');policy=s[a:s.index('\n#endif',a)]
# The system group may contain GUI services. Only the actual IO-storage ID
# may select the storage override; regressions in the wiring must fail too.
assert 'b2l_sched_initial_preferred_pset(i, thread_group_id(tg) == THREAD_GROUP_IO_STORAGE)' in s
assert 'thread_group_id(tg) == THREAD_GROUP_SYSTEM' not in s
assert 'thread_group_join_io_storage();' in (root/'xnu-overlay/bsd/kern/b2l_native_cache.c').read_text()
a=s.index('void\nsched_clutch_init_with_thread_group(');b=s.index('\n/*\n * sched_clutch_destroy()',a);group_init=s[a:b]
a=s.index('\tsched_clutch_edge edge_config_defaults[MAX_PSET_TYPES][MAX_PSET_TYPES];',s.index('sched_edge_cpu_init_completed(void)'));defaults=s[a:s.index('\n\tspl_t s = splsched();',a)]
a=s.index('\t\t\t\tsched_clutch_edge edge_config = edge_config_defaults',s.index('sched_edge_cpu_init_completed(void)'));edges=s[a:s.index('\n\t\t\t\tsched_edge_config_set',a)]
pre=r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <map>
#include <vector>
#include <algorithm>
extern "C" {
#include "DeviceTree.h"
}
#define B2L 1
#define HAS_CLUSTER 0
#define HAS_MCORE 1
#define CONFIG_SCHED_PERF_ISLANDS 1
#define CONFIG_SPTM 0
#define MAX_CPUS 8
#define MAX_CPU_CLUSTERS 3
#define MAX_CPU_CLUSTER_PHY_ID 2
#define MAX_PSET_TYPES 3
#ifndef __unused
#define __unused __attribute__((unused))
#endif
#define ML_READPROP_MANDATORY UINT64_MAX
#define kSuccess 0
#define PSET_ID_INVALID 255
#define MACHDEP_TPIDR_CPUNUM_SHIFT 0
#define MACHDEP_TPIDR_CPUNUM_MASK 255
#define MACHDEP_TPIDR_CLUSTERID_SHIFT 16
#define MACHDEP_TPIDR_CLUSTERID_MASK 0xff0000
#define MPIDR_CLUSTER_ID(p) (((p)>>8)&255)
#define MPIDR_CPU_ID(p) ((p)&255)
#define MAX(a,b) std::max(a,b)
using DTEntry=int;struct OpaqueDTEntryIterator{unsigned at;};using boolean_t=bool;
using vm_offset_t=uintptr_t;using pset_id_t=unsigned;using sched_bucket_t=unsigned;
enum cluster_type_t{CLUSTER_TYPE_SMP,CLUSTER_TYPE_E,CLUSTER_TYPE_P,CLUSTER_TYPE_M,MAX_CPU_TYPES};
enum pset_type_t{PSET_AMP_E,PSET_AMP_P,PSET_AMP_M};
enum {TH_BUCKET_FIXPRI,TH_BUCKET_SHARE_FG,TH_BUCKET_SHARE_IN,TH_BUCKET_SHARE_DF,TH_BUCKET_SHARE_UT,TH_BUCKET_SHARE_BG,TH_BUCKET_SCHED_MAX};
static bool cpu_config_correct=true,cpu_config_modified=false;static int limit=8;
static int64_t cluster_offsets[3];
#define CONFIG_SCHED_EDGE 1
#define THREAD_GROUP_IO_STORAGE 4
#define os_atomic_store(p,v,order) (*(p)=(v))
struct thread_group {unsigned id;};
static unsigned thread_group_id(thread_group*t){return t->id;}
struct sched_clutch;
struct sched_clutch_bucket_group {unsigned scbg_preferred_pset;};
using sched_clutch_bucket_group_t=sched_clutch_bucket_group*;
struct sched_clutch {unsigned sc_thr_count;sched_clutch_bucket_group sc_clutch_groups[TH_BUCKET_SCHED_MAX];thread_group*sc_tg;};
using sched_clutch_t=sched_clutch*;
static void sched_clutch_bucket_group_init(sched_clutch_bucket_group_t g,sched_clutch_t,unsigned){g->scbg_preferred_pset=0;}
using Props=std::map<std::string,std::vector<uint8_t>>;static Props props[8];
static uint32_t u32(const uint8_t*p){uint32_t v;memcpy(&v,p,4);return v;}
static size_t decode(const uint8_t*p,size_t off){
 uint32_t n=u32(p+off),kids=u32(p+off+4);off+=8;std::string name;Props v;
 for(unsigned i=0;i<n;++i){std::string key((const char*)p+off);unsigned len=u32(p+off+32);off+=36;v[key]={p+off,p+off+len};if(key=="name")name=(const char*)p+off;off+=(len+3)&~3u;}
 if(name.size()==4&&name.substr(0,3)=="cpu"&&name[3]>='0'&&name[3]<='7')props[name[3]-'0']=v;
 for(unsigned i=0;i<kids;++i)off=decode(p,off);return off;
}
static int PE_parse_boot_argn(const char*n,void*p,size_t size){if(!strcmp(n,"cpus")&&limit!=8){assert(size==4);memcpy(p,&limit,4);return 1;}return 0;}
static int SecureDTLookupEntry(void*,const char*n,DTEntry*p){assert(!strcmp(n,"/cpus"));*p=0;return 0;}
static int SecureDTInitEntryIterator(DTEntry,OpaqueDTEntryIterator*p){p->at=0;return 0;}
static int SecureDTIterateEntries(OpaqueDTEntryIterator*p,DTEntry*out){if(p->at==8)return 1;*out=p->at++;return 0;}
static bool ml_is_boot_cpu(DTEntry p){return p==0;}
static uint64_t ml_readprop(DTEntry p,const char*n,uint64_t def){auto it=props[p].find(n);if(it==props[p].end()){assert(def!=ML_READPROP_MANDATORY);return def;}assert(it->second.size()==4);return u32(it->second.data());}
static void ml_read_reg_range(DTEntry,const char*,uint64_t*base,uint64_t*len){*base=*len=0;}
static void ml_read_chip_revision(unsigned*p){*p=0;}
static void ml_cluster_power_override(unsigned*p){*p=0;}
static void write_register(const char*,uint64_t){}
'''
mid=r'''
static ml_topology_cpu_t cpus[8];static ml_topology_cluster_t clusters[3];
static ml_topology_info_t topology_info={};
struct Pset{pset_type_t pset_type;unsigned pset_id,cluster_id;};using processor_set_t=Pset*;
static Pset psets[3];static processor_set_t pset_array[3],sched_boot_pset;static unsigned sched_num_psets;
struct sched_clutch_edge {unsigned sce_migration_weight,sce_migration_allowed,sce_steal_allowed;};
static unsigned sched_edge_qos_friction_weight_us[6]={0,1,2,3,4,5};
#define assert3u(a,op,b) assert((a)op(b))
static sched_clutch_edge get_edge(unsigned src,unsigned dst,unsigned bucket){
 processor_set_t src_pset=pset_array[src],dst_pset=pset_array[dst];
'''
post=r'''
 return edge_config;
}
static void reset(){memset(cpus,0,sizeof(cpus));memset(clusters,0,sizeof(clusters));topology_info={};topology_info.cpus=cpus;topology_info.clusters=clusters;}
int main(){
 uint8_t data[8192];b2l_dt_config c={};c.memory_base=0x900000000ULL;c.memory_size=512*1024*1024;c.counter_frequency=26000000;c.video.base=0xca000000;c.video.row_bytes=5760;c.video.width=1440;c.video.height=3040;
 size_t used=0;assert(!b2l_build_device_tree(data,sizeof(data),&c,&used));assert(decode(data,0)==used);
 reset();ml_parse_cpu_topology();assert(topology_info.num_clusters==3&&topology_info.num_cpus==8);
 assert(clusters[0].cpu_mask==15&&clusters[1].cpu_mask==48&&clusters[2].cpu_mask==192);
 assert(clusters[0].cluster_type==CLUSTER_TYPE_E&&clusters[1].cluster_type==CLUSTER_TYPE_M&&clusters[2].cluster_type==CLUSTER_TYPE_P);
 for(unsigned i=0;i<8;++i){assert(cpus[i].phys_id==(i<6?i:0x100+i-6));assert(cpus[i].cluster_id==(i<4?0:i<6?1:2));assert(cpus[i].cluster_core_id==(i<4?i:i<6?i-4:i-6));}
 for(unsigned i=0;i<3;++i){psets[i]={i==0?PSET_AMP_E:i==1?PSET_AMP_M:PSET_AMP_P,i,i};pset_array[i]=&psets[i];}sched_boot_pset=&psets[0];sched_num_psets=3;
 for(unsigned id: {0u,1u,4u,99u}) {thread_group tg{id};sched_clutch clutch{};sched_clutch_init_with_thread_group(&clutch,&tg);
  assert(clutch.sc_tg==&tg);for(unsigned b=0;b<6;++b)assert(clutch.sc_clutch_groups[b].scbg_preferred_pset==(b==TH_BUCKET_SHARE_BG?0:(id==4||b==TH_BUCKET_SHARE_UT)?1:2));}
 for(unsigned b=0;b<TH_BUCKET_SCHED_MAX;++b){assert(b2l_sched_initial_preferred_pset(b,false)==(b==TH_BUCKET_SHARE_BG?0:b==TH_BUCKET_SHARE_UT?1:2));assert(b2l_sched_initial_preferred_pset(b,true)==(b==TH_BUCKET_SHARE_BG?0:1));}
 for(unsigned b=0;b<6;++b){auto pm=get_edge(2,1,b),pe=get_edge(2,0,b),me=get_edge(1,0,b);assert(pm.sce_migration_allowed&&pe.sce_migration_allowed&&me.sce_migration_allowed);assert(pm.sce_migration_weight<pe.sce_migration_weight);assert(!get_edge(0,2,b).sce_migration_allowed&&!get_edge(0,1,b).sce_migration_allowed);}
 // Partial-CPU safe fallback never returns a missing pset.
 limit=4;reset();ml_parse_cpu_topology();assert(topology_info.num_cpus==4&&topology_info.num_clusters==1&&clusters[0].cpu_mask==15);sched_num_psets=1;
 for(unsigned b=0;b<6;++b)for(unsigned k=0;k<2;++k)assert(b2l_sched_initial_preferred_pset(b,k)==0);
}
'''
with tempfile.TemporaryDirectory(prefix='b2l-edge-topology-') as td:
 p=Path(td);(p/'test.cpp').write_text(pre+structs+mid.split('static sched_clutch_edge get_edge')[0]+parser+policy+'\n'+group_init+'\nstatic sched_clutch_edge get_edge(unsigned src,unsigned dst,unsigned bucket){\n processor_set_t src_pset=pset_array[src],dst_pset=pset_array[dst];\n'+defaults+edges+post)
 flags=['-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(root/'uefi/XnuLoader'),'-I'+str(root/'common/smp')]
 subprocess.run(['clang','-std=c11',*flags,'-c',str(root/'uefi/XnuLoader/DeviceTree.c'),'-o',str(p/'dt.o')],check=True)
 subprocess.run(['clang++','-std=c++17',*flags,str(p/'test.cpp'),str(p/'dt.o'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('Real EFI -> real XNU topology:3 clusters/0f,30,c0 masks/unchanged firmware IDs; Edge QoS defaults and M4->A75->A55 spill/fallback PASS')
