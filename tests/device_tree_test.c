/* SPDX-License-Identifier: BSD-2-Clause */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../uefi/XnuLoader/DeviceTree.h"
static unsigned seed_properties, memory_map_nodes, defaults_nodes, ctrr_properties;
static unsigned boot_lun_properties, secondary_properties;
static unsigned development_security_properties, wifi_mac_properties, sleep_disabled_properties;
static unsigned cpu_nodes, cpu_ids, product_nodes, manifest_nodes;
static uint64_t read64(const unsigned char*p){uint64_t v;memcpy(&v,p,8);return v;}
static uint32_t read32(const unsigned char *p) { uint32_t n; memcpy(&n,p,4); return n; }
static size_t decode(const unsigned char *buf, size_t size, size_t off,
                     unsigned depth, unsigned *nodes, unsigned *properties, const char *parent_path)
{
    assert(depth < 8 && off + 8 <= size);
    if (!depth) { cpu_nodes=0; cpu_ids=0; }
    uint32_t nprops = read32(buf+off), children = read32(buf+off+4);
    off += 8; ++*nodes;
    const char *node_name = 0;
    char path[128];
    assert(nprops && nprops < 64 && children <= 9);
    for (uint32_t i=0; i<nprops; ++i) {
        assert(off + 36 <= size && memchr(buf+off, 0, 32));
        uint32_t len=read32(buf+off+32);
        const char *name=(const char *)buf+off;
        off += 36; assert(len <= size-off);
        if (!strcmp(name,"name")) { assert(len && buf[off+len-1]==0); node_name=(const char *)buf+off; }
        assert(strcmp(name,"clock-frequency") && strcmp(name,"bus-frequency"));
        assert(strcmp(name,"TrustCache") && strcmp(name,"AuxKC") && strcmp(name,"AuxKC-mach_header"));
        assert(strcmp(name,"pmap-io-ranges") && strcmp(name,"pmap-io-filters"));
        if (!strcmp(name,"reg") && node_name && !strcmp(node_name,"framebuffer")) {
            uint64_t pa; memcpy(&pa,buf+off,8); assert(depth==1 && len==16 && pa==0xca000000);
        }
        if (!strcmp(name,"b2l,touch-status")) { assert(len==24*8 && read64(buf+off)==1 && read64(buf+off+8)==0x123); }
        if (!strcmp(name,"crypto-hash-method")) assert(len==9 && !memcmp(buf+off,"sha2-384",9));
        if (!strcmp(name,"display-scale")) assert(len==4 && read32(buf+off)==3);
        if (!strcmp(name,"sleep-disabled")) {
            ++sleep_disabled_properties;
            assert(depth==1 && node_name && !strcmp(node_name,"defaults"));
            assert(len==4 && read32(buf+off)==1);
        }
        if (!strcmp(name,"mac-address-wifi0")) {
            static const unsigned char fixture[6]={2,0,0,0,0,1};
            ++wifi_mac_properties;
            assert(depth==1 && node_name && !strcmp(node_name,"chosen"));
            assert(len==6 && !memcmp(buf+off,fixture,6));
        }
        if (!strcmp(name,"random-seed")) { ++seed_properties; assert(len==256); for(unsigned j=0;j<256;++j)assert(buf[off+j]==(unsigned char)(j+1)); }
        if (!strcmp(name,"kernel-ctrr-to-be-enabled")) {
            ++ctrr_properties;
            assert(depth==1 && node_name && !strcmp(node_name,"chosen"));
            assert(len==4 && read32(buf+off)==0);
        }
        if (!strcmp(name,"b2l,boot-ufs-lun")) {
            ++boot_lun_properties;
            assert(depth==1 && node_name && !strcmp(node_name,"chosen"));
            assert(len==4 && read32(buf+off)==3);
        }
        if (!strcmp(name,"certificate-production-status") ||
            !strcmp(name,"effective-production-status-ap") ||
            !strcmp(name,"certificate-security-mode") ||
            !strcmp(name,"effective-security-mode-sep")) {
            assert(depth==1 && node_name && !strcmp(node_name,"chosen"));
            assert(len==4);
            unsigned security_mode = strstr(name,"security-mode") != NULL;
            assert(read32(buf+off)==security_mode);
            ++development_security_properties;
        }
        if (!strcmp(name,"b2l,native-cache")){assert(len==16);assert(read64(buf+off)==0x902000000ULL);assert(read64(buf+off+8)==128*1024*1024);}
        if (!strcmp(name,"b2l,secondary-trampoline")) {
            ++secondary_properties;
            assert(depth==1 && node_name && !strcmp(node_name,"chosen"));
            assert(len==16 && read64(buf+off)==0xceffc000 && read64(buf+off+8)==16384);
        }
        if (!strcmp(name,"timebase-frequency")) assert(len==4 && read32(buf+off)==26000000);
        if (!strcmp(name,"state")) { assert(node_name); const char *v=!strcmp(node_name,"cpu0")?"running":"stopped"; assert(len==8 && !memcmp(buf+off,v,8)); }
        assert(strcmp(name,"chip-id") && strcmp(name,"unique-chip-id") && strcmp(name,"board-id"));
        if (!strcmp(name,"product-name")) assert(len==sizeof("iPhone 16 Pro Max") && !memcmp(buf+off,"iPhone 16 Pro Max",len));
        if (!strcmp(name,"model")) assert(len==sizeof("iPhone17,2") && !memcmp(buf+off,"iPhone17,2",len));
        if (!strcmp(name,"compatible") && depth==0) assert(len==sizeof("samsung,beyond2lte") && !memcmp(buf+off,"samsung,beyond2lte",len));
        if (!strcmp(name,"home-button-type")) assert(depth==1 && node_name && !strcmp(node_name,"buttons") && len==4 && read32(buf+off)==2);
        if (!strcmp(name,"device_type")) assert(len && buf[off+len-1]==0);
        if (!strcmp(name,"device_type") && node_name && !strcmp(node_name,"disp0")) {
            assert(depth==2 && !strcmp(parent_path,"/arm-io"));
            /* Shipped IOMobileFramebuffer counts this raw data value before
             * it starts IOService matching; a generic display is ignored. */
            assert(len==sizeof("display-subsystem") && !memcmp(buf+off,"display-subsystem",len));
        }
        if (!strcmp(name,"device_type") && node_name && !strcmp(node_name,"framebuffer"))
            assert(depth==1 && len==sizeof("display") && !memcmp(buf+off,"display",len));
        if (!strcmp(name,"die-cluster-id")) assert(len==4 && read32(buf+off)==0);
        if (!strcmp(name,"cluster-core-id")) assert(len==4 && read32(buf+off)==(unsigned)(node_name[3]-'0'));
        if (!strcmp(name,"cluster-type")) assert(len==4 && read32(buf+off)=='S');
        if ((!strcmp(name,"reg") || !strcmp(name,"cpu-id")) && len==4) { assert(node_name); unsigned id=(unsigned)(node_name[3]-'0'); assert(read32(buf+off)==(id<6?id:0x100+id-6)); }
        off += (len + 3) & ~3U; ++*properties;
    }
    assert(node_name);
    if (depth==2 && !strcmp(parent_path,"/cpus")) {
        assert(strlen(node_name)==4 && !memcmp(node_name,"cpu",3));
        unsigned id=(unsigned)(node_name[3]-'0');
        assert(id<8 && !(cpu_ids&(1U<<id)));
        cpu_ids|=1U<<id; ++cpu_nodes;
    }
    if(depth==0) path[0]=0;
    else { int n=snprintf(path,sizeof(path),"%s/%s",parent_path,node_name); assert(n>0 && (size_t)n<sizeof(path)); }
    if(!strcmp(path,"/chosen/memory-map")) {
        ++memory_map_nodes; assert(nprops==1 && children==0);
    }
    if(!strcmp(path,"/product")) {
        ++product_nodes; assert(nprops==4 && children==0);
    }
    if(!strcmp(path,"/chosen/manifest-properties")) {
        ++manifest_nodes; assert(nprops==1 && children==0);
    }
    if(!strcmp(path,"/defaults")) {
        ++defaults_nodes; assert(nprops==2 && children==0);
    }
    for (uint32_t i=0; i<children; ++i) off=decode(buf,size,off,depth+1,nodes,properties,path);
    return off;
}
int main(void)
{
    unsigned char buf[8192], small[8192];
    struct b2l_dt_config c={0}; size_t used=0;
    c.memory_base=0x900000000ULL; c.memory_size=512ULL*1024*1024;
    c.mpidr=0x80000000; c.counter_frequency=26000000;
    c.has_wifi_mac=1;c.wifi_mac[0]=2;c.wifi_mac[5]=1;
    c.has_boot_ufs_lun=1;c.boot_ufs_lun=3;
    c.touch_status[0]=1;c.touch_status[1]=0x123;
    c.secondary_base=0xceffc000;c.secondary_size=16384;
    c.native_cache_base=c.memory_base+32*1024*1024;c.native_cache_size=128*1024*1024;
    c.video.base=0xca000000; c.video.row_bytes=1440*4; c.video.width=1440; c.video.height=3040;
    _Static_assert(B2L_SEED_BYTES == 256, "XNU PRNG seed length");
    for(unsigned i=0;i<B2L_SEED_BYTES;++i)c.random_seed[i]=(unsigned char)(i+1);
    assert(b2l_build_device_tree(buf,sizeof(buf),&c,&used)==0);
    unsigned nodes=0, props=0;
    assert(decode(buf,used,0,0,&nodes,&props,"")==used);
    assert(nodes==22 && props>42 && seed_properties==1);
    assert(memory_map_nodes==1 && defaults_nodes==1 && ctrr_properties==1);
    assert(boot_lun_properties==1 && secondary_properties==1);
    assert(wifi_mac_properties==1 && sleep_disabled_properties==1);
    assert(development_security_properties==4);
    assert(cpu_nodes==8 && cpu_ids==0xff);
    assert(product_nodes==1 && manifest_nodes==1);
    for(size_t capacity=0;capacity<used;++capacity) {
        size_t ignored=0; memset(small,0x5a,sizeof(small));
        assert(b2l_build_device_tree(small,capacity,&c,&ignored)<0);
        for(size_t j=capacity;j<sizeof(small);++j)assert(small[j]==0x5a);
    }
    assert(b2l_build_device_tree(small,used,&c,&used)==0);
    assert(!memcmp(buf,small,used));
    c.mpidr=0x80000100;
    assert(b2l_build_device_tree(buf,sizeof(buf),&c,&used)<0);
    c.mpidr=0x80000000;
    c.boot_ufs_lun=8;
    assert(b2l_build_device_tree(buf,sizeof(buf),&c,&used)<0);
    c.has_boot_ufs_lun=0;boot_lun_properties=0;
    assert(b2l_build_device_tree(buf,sizeof(buf),&c,&used)==0);
    nodes=0;props=0;assert(decode(buf,used,0,0,&nodes,&props,"")==used);
    assert(boot_lun_properties==0); /* Filesystem fallback must not guess LUN0. */
    c.secondary_base=0x87000000;
    assert(b2l_build_device_tree(buf,sizeof(buf),&c,&used)<0);
    c.secondary_base=0x100000000ULL;
    assert(b2l_build_device_tree(buf,sizeof(buf),&c,&used)<0);
    c.secondary_base=0xceffc000;c.secondary_size=4096;
    assert(b2l_build_device_tree(buf,sizeof(buf),&c,&used)<0);
    c.secondary_size=16384;
    c.wifi_mac[0]=1;
    assert(b2l_build_device_tree(buf,sizeof(buf),&c,&used)<0);
    c.has_wifi_mac=0;wifi_mac_properties=0;
    assert(b2l_build_device_tree(buf,sizeof(buf),&c,&used)==0);
    nodes=0;props=0;assert(decode(buf,used,0,0,&nodes,&props,"")==used);
    assert(wifi_mac_properties==0);
    c.counter_frequency=0;
    assert(b2l_build_device_tree(buf,sizeof(buf),&c,&used)<0);
    assert(sizeof(struct b2l_boot_args)==1152);
    assert(offsetof(struct b2l_boot_args,device_tree)==96);
    assert(offsetof(struct b2l_boot_args,flags)==1136);
    puts("Apple DT capacity, topology, entropy, CTRR policy, and boot_args ABI tests passed");
    return 0;
}
