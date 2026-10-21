/* SPDX-License-Identifier: BSD-2-Clause */
/* Native, read-only Exynos 9820 UFS transport. Inherit a live calibrated link;
 * never invoke firmware callbacks, reset HCE/PHY, or issue storage writes. */
#if defined(ARM64_BOARD_CONFIG_B2L)
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <kern/locks.h>
#include <kern/clock.h>
#include <vm/pmap.h>
#include <vm/vm_kern.h>
#include <vm/vm_kern_xnu.h>
#include <arm/caches_internal.h>
#include <pexpert/device_tree.h>
#include <libkern/libkern.h>
#include "b2l_ufs_protocol.h"

extern vm_offset_t ml_io_map(vm_offset_t,vm_size_t);
extern ppnum_t pmap_find_phys(pmap_t,addr64_t);
extern unsigned long gPhysBase,gPhysSize;
static LCK_GRP_DECLARE(b2l_ufs_group,"b2l_ufs_transport");
static LCK_MTX_DECLARE(b2l_ufs_mutex,&b2l_ufs_group);
static struct {
    vm_offset_t hci,vs,dma;
    uint64_t dma_pa;
    struct b2l_ufs_io io;
    struct b2l_ufs_geometry geometry;
    int initialized,error,poisoned;
} host;

static uint32_t native_read(void *unused,uint32_t offset)
{ (void)unused; return *(volatile uint32_t *)(host.hci+offset); }
static void native_write(void *unused,uint32_t offset,uint32_t value)
{ (void)unused; *(volatile uint32_t *)(host.hci+offset)=value; __builtin_arm_dsb(0xf); }
static uint32_t vs_read(uint32_t offset)
{ return *(volatile uint32_t *)(host.vs+offset); }
static void vs_write(uint32_t offset,uint32_t value)
{ *(volatile uint32_t *)(host.vs+offset)=value; __builtin_arm_dsb(0xf); }
static uint64_t native_clock(void *unused)
{ (void)unused;return mach_absolute_time(); }
static void native_pause(void *unused)
{ (void)unused;delay(1); }
static void native_sync(void *unused,int completed)
{
    (void)unused;
    /* No dirty CPU accesses to these lines between the two synchronizations. */
    if(completed)FlushPoC_DcacheRegion(host.dma,B2L_UFS_DMA_SIZE);
    else CleanPoC_DcacheRegion(host.dma,B2L_UFS_DMA_SIZE);
    __builtin_arm_dsb(0xf);
}
static int to_errno(int error)
{
    switch(error) {
    case B2L_UFS_OK:return 0;
    case B2L_UFS_BAD_ARGUMENT:return EINVAL;
    case B2L_UFS_BUSY:return EBUSY;
    case B2L_UFS_TIMEOUT:return ETIMEDOUT;
    default:return EIO;
    }
}
static int command(const uint8_t cdb[16],uint32_t bytes)
{
    struct b2l_ufs_utrd *tr=(void *)(host.dma+B2L_UFS_UTRL_OFFSET);
    struct b2l_ufs_ucd *ucd=(void *)(host.dma+B2L_UFS_UCD_OFFSET);
    int error;
    if(host.poisoned)return B2L_UFS_IO_ERROR;
    error=b2l_ufs_build_read(tr,ucd,host.dma_pa+B2L_UFS_UCD_OFFSET,
        host.dma_pa+B2L_UFS_DATA_OFFSET,(uint8_t)host.geometry.lun,0,cdb,bytes);
    if(error)return error;
    b2l_ufs_zero((void *)(host.dma+B2L_UFS_DATA_OFFSET),bytes);
    vs_write(0x40,0xffffffffU); /* SCSI nexus, preserving existing vendor format. */
    error=b2l_ufs_execute(&host.io,tr,ucd,(uint8_t)host.geometry.lun,0);
    if(error) {
        uint32_t status=native_read(NULL,0x20);
        /* An unfinished transaction may still DMA. Keep its permanent memory
         * pinned and prohibit all later commands instead of reusing it. */
        if(native_read(NULL,0x58)!=0 || (status&B2L_UFS_FATAL))host.poisoned=1;
        printf("B2L UFS: read command 0x%x failed error=%d OCS=0x%x response=0x%x status=0x%x\n",
            cdb[0],error,tr->dword[2],ucd->response[6],ucd->response[7]);
    }
    return error;
}
static int geometry(void)
{
    uint8_t cdb[16]={0};
    uint8_t *data=(void *)(host.dma+B2L_UFS_DATA_OFFSET);
    int error;
    /* A previous boot environment can leave a single UNIT ATTENTION pending.
     * Clear it with REQUEST SENSE; don't silently accept any failed command. */
    for(unsigned retry=0;retry<3;retry++) {
        bzero(cdb,sizeof(cdb));error=command(cdb,0);
        if(!error)break;
        if(error!=B2L_UFS_SCSI_ERROR || retry==2)return to_errno(error);
        cdb[0]=3;cdb[4]=18;
        error=command(cdb,18);if(error)return to_errno(error);
    }
    bzero(cdb,sizeof(cdb));cdb[0]=0x25;
    error=command(cdb,8);if(error)return to_errno(error);
    uint64_t last=b2l_ufs_be32(data);
    uint32_t size=b2l_ufs_be32(data+4);
    if(last==UINT32_MAX) {
        bzero(cdb,sizeof(cdb));cdb[0]=0x9e;cdb[1]=0x10;cdb[13]=32;
        error=command(cdb,32);if(error)return to_errno(error);
        last=b2l_ufs_be64(data);size=b2l_ufs_be32(data+8);
    }
    if(last==UINT64_MAX || !last || size<512 || size>4096 || (size&(size-1)) ||
        last+1>UINT64_MAX/size)return EIO;
    host.geometry.block_count=last+1;host.geometry.block_size=size;
    return 0;
}
static int wait_run_bit(uint32_t offset,uint32_t expected)
{
    uint64_t start=mach_absolute_time();
    while((native_read(NULL,offset)&1)!=expected) {
        if(mach_absolute_time()-start>=host.io.timeout_ticks)return ETIMEDOUT;
        delay(1);
    }
    return 0;
}
static int takeover(void)
{
    uint32_t cap=native_read(NULL,0),hce=native_read(NULL,0x34),hcs=native_read(NULL,0x30);
    uint64_t list=host.dma_pa+B2L_UFS_UTRL_OFFSET;
    uint64_t task=host.dma_pa+B2L_UFS_UTMR_OFFSET;
    if(!(cap&(1U<<24)) || !(hce&1) || (hcs&7)!=7) {
        printf("B2L UFS: inherited host unavailable CAP=0x%x HCE=0x%x HCS=0x%x\n",cap,hce,hcs);
        return ENXIO;
    }
    if(native_read(NULL,0x58) || native_read(NULL,0x78))return EBUSY;
    /* These public vendor settings were established by the working EFI UFS
     * driver. Refuse a different descriptor contract instead of guessing. */
    if(vs_read(0)!=12 || vs_read(4)!=12 || vs_read(0x60)!=0xA)return ENOTSUP;
    native_write(NULL,0x24,0); /* Disable controller interrupts, poll completion. */
    native_write(NULL,0x60,0);native_write(NULL,0x80,0);
    if(wait_run_bit(0x60,0) || wait_run_bit(0x80,0))return ETIMEDOUT;
    bzero((void *)host.dma,B2L_UFS_DMA_SIZE);native_sync(NULL,0);
    native_write(NULL,0x50,(uint32_t)list);native_write(NULL,0x54,(uint32_t)(list>>32));
    native_write(NULL,0x70,(uint32_t)task);native_write(NULL,0x74,(uint32_t)(task>>32));
    native_write(NULL,0x60,1); /* Task management remains stopped; never used. */
    if(wait_run_bit(0x60,1))return ETIMEDOUT;
    if(native_read(NULL,0x50)!=(uint32_t)list || native_read(NULL,0x54)!=(uint32_t)(list>>32) ||
        !(native_read(NULL,0x60)&1) || native_read(NULL,0x70)!=(uint32_t)task ||
        native_read(NULL,0x74)!=(uint32_t)(task>>32))return EIO;
    return 0;
}
int b2l_ufs_transport_init(struct b2l_ufs_geometry *out)
{
    DTEntry chosen;
    const uint32_t *lun;
    const void *property;
    unsigned size;
    int error=0;
    if(!out)return EINVAL;
    lck_mtx_lock(&b2l_ufs_mutex);
    if(host.initialized) { error=host.error;goto done; }
    host.initialized=1;
    if(SecureDTLookupEntry(NULL,"/chosen",&chosen)!=kSuccess ||
        SecureDTGetProperty(chosen,"b2l,boot-ufs-lun",&property,&size)!=kSuccess || size!=4) {
        error=ENXIO;goto done;
    }
    lun=property;
    if(*lun>7) { error=ENXIO;goto done; }
    host.geometry.lun=*lun;
    host.hci=ml_io_map(0x13d60000,4096);host.vs=ml_io_map(0x13d61000,4096);
    if(!host.hci || !host.vs) { error=ENOMEM;goto done; }
    host.vs+=0x100; /* Exact public vendor register window starts at0x13d61100. */
    if(kmem_alloc_contig(kernel_map,&host.dma,B2L_UFS_DMA_SIZE,PAGE_MASK,0,0,
        KMA_DATA|KMA_ZERO|KMA_PERMANENT,VM_KERN_MEMORY_IOKIT)!=KERN_SUCCESS) {
        error=ENOMEM;goto done;
    }
    ppnum_t page=pmap_find_phys(kernel_pmap,(addr64_t)host.dma);
    if(!page) { error=ENOMEM;goto done; }
    host.dma_pa=ptoa_64(page);
    if((host.dma_pa&4095) || host.dma_pa<gPhysBase || B2L_UFS_DMA_SIZE>gPhysSize ||
        host.dma_pa-gPhysBase>gPhysSize-B2L_UFS_DMA_SIZE) { error=EFAULT;goto done; }
    for(uint32_t off=0;off<B2L_UFS_DMA_SIZE;off+=PAGE_SIZE) {
        if(ptoa_64(pmap_find_phys(kernel_pmap,(addr64_t)host.dma+off))!=host.dma_pa+off) {
            error=EFAULT;goto done;
        }
    }
    host.io=(struct b2l_ufs_io){NULL,native_read,native_write,native_clock,native_pause,native_sync,0};
    nanoseconds_to_absolutetime(5ULL*NSEC_PER_SEC,&host.io.timeout_ticks);
    if(!host.io.timeout_ticks) { error=EIO;goto done; }
    error=takeover();if(!error)error=geometry();
    if(!error)printf("B2L UFS: native read-only LUN %u block=%u capacity=%llu blocks\n",
        host.geometry.lun,host.geometry.block_size,host.geometry.block_count);
done:
    host.error=error;
    if(!error)*out=host.geometry;
    lck_mtx_unlock(&b2l_ufs_mutex);
    return error;
}
int b2l_ufs_transport_read(uint64_t lba,uint32_t blocks,void *out)
{
    uint8_t cdb[16]={0};
    int error;
    lck_mtx_lock(&b2l_ufs_mutex);
    if(!host.initialized || host.error || host.poisoned) { error=ENXIO;goto done; }
    if(lba>host.geometry.block_count || blocks>host.geometry.block_count-lba ||
        blocks>B2L_UFS_MAX_READ_BYTES/host.geometry.block_size || (blocks&&!out)) {
        error=EINVAL;goto done;
    }
    if(!blocks) { error=0;goto done; }
    if(lba<=UINT32_MAX && (uint64_t)blocks-1<=UINT32_MAX-lba) {
        cdb[0]=0x28;b2l_ufs_be32_store(cdb+2,(uint32_t)lba);
        cdb[7]=(uint8_t)(blocks>>8);cdb[8]=(uint8_t)blocks;
    } else {
        cdb[0]=0x88;b2l_ufs_be32_store(cdb+2,(uint32_t)(lba>>32));
        b2l_ufs_be32_store(cdb+6,(uint32_t)lba);b2l_ufs_be32_store(cdb+10,blocks);
    }
    uint32_t bytes=blocks*host.geometry.block_size;
    error=to_errno(command(cdb,bytes));
    if(!error)memcpy(out,(void *)(host.dma+B2L_UFS_DATA_OFFSET),bytes);
done:
    lck_mtx_unlock(&b2l_ufs_mutex);return error;
}
#endif
