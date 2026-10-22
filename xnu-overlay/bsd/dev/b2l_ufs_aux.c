/* SPDX-License-Identifier: MIT
 * Explicit SYSTEM auxiliary layout; never infer writable space from free blocks.
 */
#include "b2l_ufs_aux.h"
#ifdef B2L_UFS_HOST_TEST
#include <errno.h>
#include <string.h>
#else
#include <sys/errno.h>
#include <libkern/libkern.h>
#endif
static uint32_t aux_be32(const unsigned char *p)
{return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
int b2l_ufs_aux_layout(const struct b2l_gpt*g,unsigned root,uint32_t bs,uint64_t capacity,struct b2l_ufs_aux_layout*out)
{
 if(out)memset(out,0,sizeof(*out));
 if(!g||!out||!root||bs<512||bs>4096||(bs&(bs-1))||g->partition_count>B2L_GPT_MAX_ENTRIES)return EINVAL;
 const struct b2l_gpt_partition *system=NULL,*user=NULL;unsigned found=0,roots=0;
 for(unsigned i=0;i<g->partition_count;i++){
  const struct b2l_gpt_partition*p=&g->partitions[i];
  if(b2l_gpt_label_equal(p->name,"system")){system=p;found++;}
  if(p->index==root){user=p;roots++;}
 }
 if(found!=1||roots!=1||!b2l_gpt_label_equal(user->name,"userdata")||system->index==root)return EINVAL;
 if(!system->index||system->first_lba>UINT64_MAX/bs||system->block_count>UINT64_MAX/bs)return EINVAL;
 uint64_t base=system->first_lba*bs,size=system->block_count*bs;
 if(base>capacity||size>capacity-base||size<=B2L_SYSTEM_LOG_BYTES+1024*1024||size%4096)return EINVAL;
 if(user->first_lba>UINT64_MAX/bs||user->block_count>UINT64_MAX/bs)return EINVAL;
 uint64_t ub=user->first_lba*bs,us=user->block_count*bs;
 if(ub>capacity||us>capacity-ub||!us||!(base+size<=ub||ub+us<=base))return EINVAL;
 out->partition_base=base;out->partition_size=size;out->hfs_size=size-B2L_SYSTEM_LOG_BYTES;
 out->log_base=base+out->hfs_size;out->log_size=B2L_SYSTEM_LOG_BYTES;out->index=system->index;return 0;
}
static int aux_hfs_one(const unsigned char*h,uint64_t expected)
{
 if(!h||h[0]!='H'||!((h[1]=='+'&&h[2]==0&&h[3]==4)||(h[1]=='X'&&h[2]==0&&h[3]==5)))return EINVAL;
 uint32_t bs=aux_be32(h+40),n=aux_be32(h+44),free=aux_be32(h+48);
 if(!(aux_be32(h+4)&(1U<<13))||bs<512||bs>65536||(bs&(bs-1))||!n||free>n||(uint64_t)bs*n!=expected)return EINVAL;
 return 0;
}
int b2l_ufs_aux_hfs(const unsigned char*primary,const unsigned char*alternate,uint64_t expected)
{
 if(aux_hfs_one(primary,expected)||aux_hfs_one(alternate,expected))return EINVAL;
 return memcmp(primary,alternate,4)||memcmp(primary+40,alternate+40,8)?EINVAL:0;
}
