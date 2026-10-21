/* SPDX-License-Identifier: BSD-2-Clause */
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "b2l_aks_core.h"
static void zero_reply(const struct b2l_aks_reply*r){
 assert(!r->scalar_count&&!r->structure_size);
 for(size_t i=0;i<4;i++)assert(!r->scalars[i]);
 for(size_t i=0;i<256;i++)assert(!r->structure[i]);
}
int main(void){
 struct b2l_aks_client s;struct b2l_aks_reply r;struct b2l_aks_call c={0};
 uint64_t n=0;b2l_aks_client_init(&s);
 c.selector=14;c.scalar_output_capacity=1;
 assert(b2l_aks_dispatch(&s,&c,&r)==B2L_AKS_NOT_OPEN);zero_reply(&r);
 c.selector=0;c.scalar_output_capacity=0;
 assert(b2l_aks_dispatch(&s,&c,&r)==B2L_AKS_SUCCESS&&s.opened);zero_reply(&r);
 assert(b2l_aks_dispatch(&s,&c,&r)==B2L_AKS_SUCCESS);
 c.selector=14;c.scalar_output_capacity=1;
 assert(b2l_aks_dispatch(&s,&c,&r)==B2L_AKS_SUCCESS&&r.scalar_count==1&&r.scalars[0]==0);
 c.scalar_input=&n;c.scalar_input_count=1;
 assert(b2l_aks_dispatch(&s,&c,&r)==B2L_AKS_SUCCESS);
 n=1;assert(b2l_aks_dispatch(&s,&c,&r)==B2L_AKS_NOT_FOUND);zero_reply(&r);
 n=UINT64_MAX;assert(b2l_aks_dispatch(&s,&c,&r)==B2L_AKS_NOT_FOUND);
 n=0;c.selector=7;assert(b2l_aks_dispatch(&s,&c,&r)==B2L_AKS_SUCCESS&&r.scalars[0]==4);
 c.selector=6;assert(b2l_aks_dispatch(&s,&c,&r)==B2L_AKS_UNSUPPORTED);zero_reply(&r);
 /* Exhaust actual shapes; only the three documented shapes may succeed. */
 for(unsigned sel=0;sel<256;sel++)for(unsigned in=0;in<6;in++)for(unsigned out=0;out<6;out++)for(unsigned st=0;st<3;st++){
   uint64_t a[6]={0};uint8_t bytes[2]={0};struct b2l_aks_client q={1,0};
   struct b2l_aks_call x={sel,a,in,bytes,st,out,0};memset(&r,0xa5,sizeof r);
   enum b2l_aks_result z=b2l_aks_dispatch(&q,&x,&r);
   int expected=(sel==0&&in==0&&out==0&&st==0)||(sel==14&&in<=1&&out==1&&st==0)||(sel==7&&in==1&&out==1&&st==0);
   assert((z==B2L_AKS_SUCCESS)==expected);if(!expected)zero_reply(&r);
 }
 b2l_aks_client_close(&s);c=(struct b2l_aks_call){0};assert(b2l_aks_dispatch(&s,&c,&r)==B2L_AKS_NOT_OPEN);
 b2l_aks_client_init(&s);c.scalar_input_count=1;c.scalar_input=&n;
 assert(b2l_aks_dispatch(&s,&c,&r)==B2L_AKS_BAD_ARGUMENT&&!s.opened);
 c=(struct b2l_aks_call){0};c.structure_output_capacity=1;
 assert(b2l_aks_dispatch(&s,&c,&r)==B2L_AKS_BAD_ARGUMENT&&!s.opened);
 assert(b2l_aks_dispatch(0,&c,&r)==B2L_AKS_BAD_ARGUMENT);zero_reply(&r);
 assert(b2l_aks_dispatch(&s,0,&r)==B2L_AKS_BAD_ARGUMENT);
 assert(b2l_aks_dispatch(&s,&c,0)==B2L_AKS_BAD_ARGUMENT);
 return 0;
}
