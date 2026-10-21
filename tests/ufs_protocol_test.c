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
    cdb[0]=0x28;
    assert(b2l_ufs_build_read(&f.tr,&f.ucd,0x1000,0x2000,0,1,cdb,4096)==B2L_UFS_BAD_ARGUMENT);
    assert(b2l_ufs_build_read(&f.tr,&f.ucd,0x1000,0x2000,0,0,cdb,65537)==B2L_UFS_BAD_ARGUMENT);
    assert(b2l_ufs_build_read(&f.tr,&f.ucd,0x1000,UINT64_MAX-4095,0,0,cdb,4096)==B2L_UFS_BAD_ARGUMENT);
    assert(b2l_ufs_build_read(&f.tr,&f.ucd,0x1001,0x2000,0,0,cdb,4096)==B2L_UFS_BAD_ARGUMENT);
    assert(b2l_ufs_build_read(&f.tr,&f.ucd,0x1000,0x2000,8,0,cdb,4096)==B2L_UFS_BAD_ARGUMENT);
    puts("Native UFS descriptor, readonly opcode, status, deadline, and DMA bounds tests passed");
}
