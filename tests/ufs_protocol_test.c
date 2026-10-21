/* SPDX-License-Identifier: BSD-2-Clause */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "b2l_ufs_protocol.h"
struct fake {
    struct b2l_ufs_utrd tr;
    struct b2l_ufs_ucd ucd;
    uint64_t ticks;
    unsigned doorbell,submitted,polls,completion,pre_sync,post_sync;
    uint32_t status;
};
static uint32_t rd(void *context,uint32_t off)
{
    struct fake *f=context;
    if(off==0x20)return f->status;
    assert(off==0x58);
    if(f->submitted && f->doorbell && f->completion && ++f->polls>=f->completion) {
        f->doorbell=0;f->tr.dword[2]=0;
        f->ucd.response[0]=0x21;f->ucd.response[2]=3;f->ucd.response[3]=0;
    }
    return f->doorbell;
}
static void wr(void *context,uint32_t off,uint32_t value)
{
    struct fake *f=context;
    if(off==0x20) { f->status&=~value;return; }
    assert(off==0x58 && value==1 && !f->submitted && f->pre_sync==1);
    f->submitted=1;f->doorbell=1;
}
static uint64_t clock_tick(void *context) { return ((struct fake *)context)->ticks; }
static void pause_tick(void *context) { ++((struct fake *)context)->ticks; }
static void sync_dma(void *context,int after)
{ struct fake *f=context;if(after)++f->post_sync;else ++f->pre_sync; }
static struct b2l_ufs_io io_for(struct fake *f)
{ return (struct b2l_ufs_io){f,rd,wr,clock_tick,pause_tick,sync_dma,5}; }
static void prepare(struct fake *f,uint32_t bytes)
{
    uint8_t cdb[16]={0x28};memset(f,0,sizeof(*f));f->completion=3;
    assert(!b2l_ufs_build_read(&f->tr,&f->ucd,0x950002000ULL,0x950003000ULL,3,0,cdb,bytes));
}
int main(void)
{
    struct fake f;prepare(&f,65536);
    assert(sizeof(f.tr)==32 && sizeof(f.ucd)==2304);
    assert(f.tr.dword[0]==0x05000000 && f.tr.dword[2]==15);
    assert(f.tr.command_low==0x50002000 && f.tr.command_high==9);
    assert(f.tr.response_offset==1024 && f.tr.response_length==1024);
    assert(f.tr.prdt_offset==2048 && f.tr.prdt_length==16*16);
    assert(f.ucd.command[1]==0x40 && b2l_ufs_be32(f.ucd.command+12)==65536);
    for(unsigned i=0;i<16;i++) {
        assert(f.ucd.prdt[i].high==9);
        assert(f.ucd.prdt[i].low==0x50003000+i*4096);
        assert(f.ucd.prdt[i].reserved==0 && f.ucd.prdt[i].length_minus_one==4095);
    }
    struct b2l_ufs_io io=io_for(&f);
    assert(!b2l_ufs_execute(&io,&f.tr,&f.ucd,3,0));
    assert(f.submitted==1 && f.post_sync==1 && f.ticks==2);
    f.tr.dword[2]=1;assert(b2l_ufs_result(&f.tr,&f.ucd,3,0)==B2L_UFS_IO_ERROR);
    f.tr.dword[2]=0;f.ucd.response[3]=8;assert(b2l_ufs_result(&f.tr,&f.ucd,3,0)==B2L_UFS_IO_ERROR);
    f.ucd.response[3]=0;f.ucd.response[2]=4;assert(b2l_ufs_result(&f.tr,&f.ucd,3,0)==B2L_UFS_IO_ERROR);
    f.ucd.response[2]=3;f.ucd.response[7]=2;assert(b2l_ufs_result(&f.tr,&f.ucd,3,0)==B2L_UFS_SCSI_ERROR);
    f.ucd.response[7]=0;f.ucd.response[15]=1;assert(b2l_ufs_result(&f.tr,&f.ucd,3,0)==B2L_UFS_IO_ERROR);
    prepare(&f,8);f.completion=0;io=io_for(&f);
    assert(b2l_ufs_execute(&io,&f.tr,&f.ucd,3,0)==B2L_UFS_TIMEOUT && f.ticks==5 && !f.post_sync);
    prepare(&f,8);f.completion=0;f.ticks=UINT64_MAX-2;io=io_for(&f);
    assert(b2l_ufs_execute(&io,&f.tr,&f.ucd,3,0)==B2L_UFS_TIMEOUT && f.ticks==2);
    prepare(&f,8);f.status=1U<<16;io=io_for(&f);
    assert(b2l_ufs_execute(&io,&f.tr,&f.ucd,3,0)==B2L_UFS_IO_ERROR && !f.submitted);
    prepare(&f,8);f.doorbell=2;io=io_for(&f);
    assert(b2l_ufs_execute(&io,&f.tr,&f.ucd,3,0)==B2L_UFS_BUSY && !f.submitted);
    uint8_t cdb[16]={0x2a};
    assert(b2l_ufs_build_read(&f.tr,&f.ucd,0x1000,0x2000,0,0,cdb,4096)==B2L_UFS_BAD_ARGUMENT);
    assert(!b2l_ufs_build_write(&f.tr,&f.ucd,0x950002000ULL,0x950003000ULL,3,0,cdb,65536));
    assert(f.tr.dword[0]==0x03000000 && f.ucd.command[1]==0x20);
    assert(f.tr.prdt_offset==2048 && f.tr.prdt_length==256);
    assert(b2l_ufs_be32(f.ucd.command+12)==65536 && f.ucd.command[16]==0x2a);
    for(unsigned i=0;i<16;i++)assert(f.ucd.prdt[i].length_minus_one==4095);
    assert(b2l_ufs_build_write(&f.tr,&f.ucd,0x1000,0x2000,0,0,cdb,0)==B2L_UFS_BAD_ARGUMENT);
    /* The exact native CDB encoder crosses the32-bit LBA boundary without
     * truncation and never uses WRITE(10)'s special zero-block count. */
    assert(!b2l_ufs_rw_cdb(cdb,0x12345678,16,1));
    assert(cdb[0]==0x2a && cdb[1]==0 && b2l_ufs_be32(cdb+2)==0x12345678);
    assert(cdb[7]==0 && cdb[8]==16);
    assert(!b2l_ufs_rw_cdb(cdb,UINT32_MAX,2,1));
    assert(cdb[0]==0x8a && b2l_ufs_be64(cdb+2)==UINT32_MAX && b2l_ufs_be32(cdb+10)==2);
    assert(!b2l_ufs_build_write(&f.tr,&f.ucd,0x1000,0x2000,0,0,cdb,8192));
    assert(f.tr.dword[0]==0x03000000 && f.ucd.command[16]==0x8a);
    assert(!b2l_ufs_rw_cdb(cdb,0x123456789ULL,8,0));
    assert(cdb[0]==0x88 && b2l_ufs_be64(cdb+2)==0x123456789ULL);
    assert(!b2l_ufs_rw_cdb(cdb,1,65536,1));assert(cdb[0]==0x8a);
    assert(b2l_ufs_rw_cdb(cdb,UINT64_MAX,2,1)==B2L_UFS_BAD_ARGUMENT);
    assert(b2l_ufs_rw_cdb(cdb,0,0,1)==B2L_UFS_BAD_ARGUMENT);
    /* Completed write, rejected SCSI status, and failed/timeout flush all use
     * the same strict submission/completion engine as reads. */
    memset(&f,0,sizeof(f));f.completion=1;cdb[0]=0x2a;
    assert(!b2l_ufs_build_write(&f.tr,&f.ucd,0x1000,0x2000,3,0,cdb,4096));io=io_for(&f);
    assert(!b2l_ufs_execute(&io,&f.tr,&f.ucd,3,0) && f.pre_sync==1 && f.post_sync==1);
    f.ucd.response[7]=2;assert(b2l_ufs_result(&f.tr,&f.ucd,3,0)==B2L_UFS_SCSI_ERROR);
    f.ucd.response[7]=0;f.ucd.response[1]=0x20;
    assert(b2l_ufs_result(&f.tr,&f.ucd,3,0)==B2L_UFS_IO_ERROR);
    memset(&f,0,sizeof(f));f.completion=1;memset(cdb,0,sizeof(cdb));cdb[0]=0x35;
    assert(!b2l_ufs_build_read(&f.tr,&f.ucd,0x1000,0x2000,3,0,cdb,0));
    assert(f.tr.dword[0]==0x01000000 && !f.tr.prdt_length && !f.ucd.command[1]);
    assert(f.ucd.command[16]==0x35 && f.ucd.command[17]==0);io=io_for(&f);
    assert(!b2l_ufs_execute(&io,&f.tr,&f.ucd,3,0));
    f.tr.dword[2]=1;assert(b2l_ufs_result(&f.tr,&f.ucd,3,0)==B2L_UFS_IO_ERROR);
    memset(&f,0,sizeof(f));
    assert(!b2l_ufs_build_read(&f.tr,&f.ucd,0x1000,0x2000,3,0,cdb,0));io=io_for(&f);
    assert(b2l_ufs_execute(&io,&f.tr,&f.ucd,3,0)==B2L_UFS_TIMEOUT);
    assert(b2l_ufs_build_write(&f.tr,&f.ucd,0x1000,0x2000,3,0,cdb,4096)==B2L_UFS_BAD_ARGUMENT);
    assert(b2l_ufs_build_read(&f.tr,&f.ucd,0x1000,0x2000,3,0,cdb,4096)==B2L_UFS_BAD_ARGUMENT);
    cdb[0]=0x42;assert(b2l_ufs_build_write(&f.tr,&f.ucd,0x1000,0x2000,3,0,cdb,4096)==B2L_UFS_BAD_ARGUMENT);
    cdb[0]=4;assert(b2l_ufs_build_write(&f.tr,&f.ucd,0x1000,0x2000,3,0,cdb,4096)==B2L_UFS_BAD_ARGUMENT);
    struct b2l_ufs_geometry g={1000,4096,3};
    assert(!b2l_ufs_transfer_bounds(&g,984,16,&f));
    assert(b2l_ufs_transfer_bounds(&g,984,17,&f)==B2L_UFS_BAD_ARGUMENT);
    assert(b2l_ufs_transfer_bounds(&g,999,2,&f)==B2L_UFS_BAD_ARGUMENT);
    assert(b2l_ufs_transfer_bounds(&g,UINT64_MAX,1,&f)==B2L_UFS_BAD_ARGUMENT);
    assert(b2l_ufs_transfer_bounds(&g,0,1,NULL)==B2L_UFS_BAD_ARGUMENT);
    assert(!b2l_ufs_transfer_bounds(&g,1000,0,NULL));
    assert(b2l_ufs_transfer_bounds(&g,1001,0,NULL)==B2L_UFS_BAD_ARGUMENT);
    cdb[0]=0x28;
    assert(b2l_ufs_build_write(&f.tr,&f.ucd,0x1000,0x2000,0,0,cdb,4096)==B2L_UFS_BAD_ARGUMENT);
    assert(b2l_ufs_build_read(&f.tr,&f.ucd,0x1000,0x2000,0,1,cdb,4096)==B2L_UFS_BAD_ARGUMENT);
    assert(b2l_ufs_build_read(&f.tr,&f.ucd,0x1000,0x2000,0,0,cdb,65537)==B2L_UFS_BAD_ARGUMENT);
    assert(b2l_ufs_build_read(&f.tr,&f.ucd,0x1000,UINT64_MAX-4095,0,0,cdb,4096)==B2L_UFS_BAD_ARGUMENT);
    assert(b2l_ufs_build_read(&f.tr,&f.ucd,0x1001,0x2000,0,0,cdb,4096)==B2L_UFS_BAD_ARGUMENT);
    assert(b2l_ufs_build_read(&f.tr,&f.ucd,0x1000,0x2000,8,0,cdb,4096)==B2L_UFS_BAD_ARGUMENT);
    puts("Native UFS read/write/flush descriptors, CDB bounds, status, deadlines, and DMA tests passed");
}
