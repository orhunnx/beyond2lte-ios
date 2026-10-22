/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include "b2l_ufs_aux.h"
static void be32(unsigned char*p,uint32_t n){p[0]=n>>24;p[1]=n>>16;p[2]=n>>8;p[3]=n;}
static void header(unsigned char*p,uint64_t size){memset(p,0,512);p[0]='H';p[1]='+';p[3]=4;be32(p+4,1<<13);be32(p+40,4096);be32(p+44,size/4096);be32(p+48,size/4096-10);}
int main(void){
 struct b2l_gpt g={.partition_count=2};g.partitions[0]=(struct b2l_gpt_partition){.index=25,.first_lba=100,.block_count=100000};strcpy(g.partitions[0].name,"system");g.partitions[1]=(struct b2l_gpt_partition){.index=31,.first_lba=200000,.block_count=100000};strcpy(g.partitions[1].name,"userdata");
 struct b2l_ufs_aux_layout out;uint64_t capacity=400000ULL*4096;
 assert(!b2l_ufs_aux_layout(&g,31,4096,capacity,&out));assert(out.index==25&&out.partition_base==100ULL*4096&&out.log_base+out.log_size==out.partition_base+out.partition_size&&out.log_size==16777216&&out.hfs_size+out.log_size==out.partition_size);
 unsigned char a[512],b[512];header(a,out.hfs_size);memcpy(b,a,512);assert(!b2l_ufs_aux_hfs(a,b,out.hfs_size));a[4+2]=0;assert(b2l_ufs_aux_hfs(a,b,out.hfs_size)==EINVAL);header(a,out.hfs_size);be32(a+44,out.partition_size/4096);assert(b2l_ufs_aux_hfs(a,b,out.hfs_size)==EINVAL);header(a,out.hfs_size);a[0]='B';assert(b2l_ufs_aux_hfs(a,b,out.hfs_size)==EINVAL);header(a,out.hfs_size);be32(b+48,UINT32_MAX);assert(b2l_ufs_aux_hfs(a,b,out.hfs_size)==EINVAL);
 assert(b2l_ufs_aux_layout(&g,25,4096,capacity,&out)==EINVAL&&!out.log_size);assert(b2l_ufs_aux_layout(&g,0,4096,capacity,&out)==EINVAL);
 for(unsigned mode=0;mode<9;mode++){
  struct b2l_gpt q=g;
  switch(mode){case 0:q.partition_count=1;break;case 1:q.partition_count=3;q.partitions[2]=q.partitions[0];q.partitions[2].index=26;break;case 2:q.partitions[1].first_lba=500;break;case 3:q.partitions[0].first_lba=UINT64_MAX;break;case 4:q.partitions[0].block_count=UINT64_MAX;break;case 5:q.partitions[0].block_count=16;break;case 6:q.partitions[1].block_count=UINT64_MAX;break;case 7:q.partitions[1].index=30;break;case 8:q.partitions[0].first_lba=400000;break;}
  memset(&out,0xa5,sizeof(out));assert(b2l_ufs_aux_layout(&q,31,4096,capacity,&out)==EINVAL&&!out.log_size);
 }
 assert(b2l_ufs_aux_layout(NULL,31,4096,capacity,&out)==EINVAL);assert(b2l_ufs_aux_layout(&g,31,768,capacity,&out)==EINVAL);assert(b2l_ufs_aux_layout(&g,31,4096,capacity,NULL)==EINVAL);
 return 0;
}
