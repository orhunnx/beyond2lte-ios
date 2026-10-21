/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef B2L_UFS_PROTOCOL_H
#define B2L_UFS_PROTOCOL_H
#include "b2l_ufs_transport.h"
/* Samsung's inherited byte-granular descriptor format, not generic DWORD
 * offsets. Preserve VS_DATA_REORDER and PRDT entry-size configuration. */
#define B2L_UFS_UPIU_SIZE 1024U
#define B2L_UFS_PRDT_COUNT (B2L_UFS_MAX_READ_BYTES / 4096U)
#define B2L_UFS_UTRL_OFFSET 0U
#define B2L_UFS_UTMR_OFFSET 4096U
#define B2L_UFS_UCD_OFFSET 8192U
#define B2L_UFS_DATA_OFFSET 12288U
#define B2L_UFS_DMA_SIZE (B2L_UFS_DATA_OFFSET + B2L_UFS_MAX_READ_BYTES)
#define B2L_UFS_FATAL ((1U<<2)|(1U<<7)|(1U<<11)|(1U<<16)|(1U<<17))
enum { B2L_UFS_OK, B2L_UFS_BAD_ARGUMENT, B2L_UFS_IO_ERROR,
       B2L_UFS_TIMEOUT, B2L_UFS_BUSY, B2L_UFS_SCSI_ERROR };
struct b2l_ufs_prdt { uint32_t low, high, reserved, length_minus_one; };
struct b2l_ufs_utrd {
    uint32_t dword[4], command_low, command_high;
    uint16_t response_length, response_offset, prdt_length, prdt_offset;
};
struct b2l_ufs_ucd {
    uint8_t command[B2L_UFS_UPIU_SIZE], response[B2L_UFS_UPIU_SIZE];
    struct b2l_ufs_prdt prdt[B2L_UFS_PRDT_COUNT];
};
_Static_assert(sizeof(struct b2l_ufs_utrd)==32,"UTRD ABI");
_Static_assert(sizeof(struct b2l_ufs_ucd)==2304,"Samsung UCD ABI");
struct b2l_ufs_io {
    void *context;
    uint32_t (*read)(void *, uint32_t);
    void (*write)(void *, uint32_t, uint32_t);
    uint64_t (*clock)(void *);
    void (*pause)(void *);
    void (*sync)(void *, int); /* 0: before DMA, 1: completed DMA */
    uint64_t timeout_ticks;
};
static inline void b2l_ufs_zero(void *p, uint32_t length)
{ uint8_t *b=p; while(length--) *b++=0; }
static inline void b2l_ufs_be32_store(uint8_t *p,uint32_t v)
{ p[0]=(uint8_t)(v>>24);p[1]=(uint8_t)(v>>16);p[2]=(uint8_t)(v>>8);p[3]=(uint8_t)v; }
static inline uint32_t b2l_ufs_be32(const uint8_t *p)
{ return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3]; }
static inline uint64_t b2l_ufs_be64(const uint8_t *p)
{ return (uint64_t)b2l_ufs_be32(p)<<32|b2l_ufs_be32(p+4); }
static inline int b2l_ufs_read_opcode(const uint8_t cdb[16])
{
    return cdb[0]==0x00 || cdb[0]==0x03 || cdb[0]==0x25 ||
        cdb[0]==0x28 || cdb[0]==0x88 || (cdb[0]==0x9e && cdb[1]==0x10);
}
static inline int b2l_ufs_build_read(struct b2l_ufs_utrd *tr,
    struct b2l_ufs_ucd *ucd,uint64_t command_pa,uint64_t data_pa,
    uint8_t lun,uint8_t tag,const uint8_t cdb[16],uint32_t bytes)
{
    uint32_t n,i,part;
    if (!tr || !ucd || !cdb || lun>7 || tag!=0 || bytes>B2L_UFS_MAX_READ_BYTES ||
        !b2l_ufs_read_opcode(cdb) || (command_pa&127) || (data_pa&4095) ||
        data_pa>UINT64_MAX-bytes) return B2L_UFS_BAD_ARGUMENT;
    b2l_ufs_zero(tr,sizeof(*tr));b2l_ufs_zero(ucd,sizeof(*ucd));
    tr->dword[0]=(bytes?0x04000000U:0U)|0x01000000U;
    tr->dword[2]=15;
    tr->command_low=(uint32_t)command_pa;tr->command_high=(uint32_t)(command_pa>>32);
    tr->response_length=1024;tr->response_offset=1024;
    n=(bytes+4095)/4096;
    tr->prdt_length=(uint16_t)(n*sizeof(struct b2l_ufs_prdt));
    tr->prdt_offset=bytes?2048:0;
    ucd->command[0]=1;ucd->command[1]=bytes?0x40:0;
    ucd->command[2]=lun;ucd->command[3]=tag;
    b2l_ufs_be32_store(ucd->command+12,bytes);
    for(i=0;i<16;i++)ucd->command[16+i]=cdb[i];
    for(i=0;i<n;i++) {
        uint64_t pa=data_pa+(uint64_t)i*4096;
        part=bytes-i*4096;if(part>4096)part=4096;
        ucd->prdt[i].low=(uint32_t)pa;ucd->prdt[i].high=(uint32_t)(pa>>32);
        ucd->prdt[i].length_minus_one=part-1;
    }
    return B2L_UFS_OK;
}
static inline int b2l_ufs_result(const struct b2l_ufs_utrd *tr,
    const struct b2l_ufs_ucd *ucd,uint8_t lun,uint8_t tag)
{
    const uint8_t *r=ucd->response;
    if ((tr->dword[2]&255)!=0 || r[0]!=0x21 || r[2]!=lun || r[3]!=tag || r[6] ||
        (r[1]&0x60) || b2l_ufs_be32(r+12)!=0) return B2L_UFS_IO_ERROR;
    if(r[7])return B2L_UFS_SCSI_ERROR;
    return B2L_UFS_OK;
}
/* All controller writes here are host-control or read-command submission.
 * No path accepts an outgoing SCSI write or a UFS query write. */
static inline int b2l_ufs_execute(struct b2l_ufs_io *io,
    struct b2l_ufs_utrd *tr,struct b2l_ufs_ucd *ucd,uint8_t lun,uint8_t tag)
{
    uint64_t start;
    uint32_t status;
    if(io->read(io->context,0x58)!=0)return B2L_UFS_BUSY;
    status=io->read(io->context,0x20);
    if(status&B2L_UFS_FATAL)return B2L_UFS_IO_ERROR;
    io->write(io->context,0x20,status);
    io->sync(io->context,0);
    start=io->clock(io->context);
    io->write(io->context,0x58,1);
    for(;;) {
        status=io->read(io->context,0x20);
        if(status&B2L_UFS_FATAL)return B2L_UFS_IO_ERROR;
        if(!(io->read(io->context,0x58)&1))break;
        if(io->clock(io->context)-start>=io->timeout_ticks)return B2L_UFS_TIMEOUT;
        io->pause(io->context);
    }
    io->sync(io->context,1);
    io->write(io->context,0x20,status);
    return b2l_ufs_result(tr,ucd,lun,tag);
}
#endif
