/* SPDX-License-Identifier: BSD-2-Clause
 * Original minimal iOS 26 no-passcode development state service.
 * These are state queries, not a keybag serialization or crypto implementation.
 */
#include "b2l_aks_core.h"

void b2l_aks_client_init(struct b2l_aks_client *s)
{ if(s){s->opened=0;s->closed=0;} }
void b2l_aks_client_close(struct b2l_aks_client *s)
{ if(s){s->opened=0;s->closed=1;} }
static void clear_reply(struct b2l_aks_reply *r)
{
    size_t i;for(i=0;i<B2L_AKS_MAX_SCALARS;i++)r->scalars[i]=0;
    for(i=0;i<B2L_AKS_MAX_STRUCTURE;i++)r->structure[i]=0;
    r->scalar_count=0;r->structure_size=0;
}
/* The scalar ABI carries handles as sign-extended int32 values. Only the
 * preloaded volatile system bag (handle zero) exists in this development mode. */
static int system_handle(uint64_t n){return n==0;}
enum b2l_aks_result b2l_aks_dispatch(struct b2l_aks_client *s,
    const struct b2l_aks_call *c,struct b2l_aks_reply *r)
{
    if(r)clear_reply(r);
    if(!s||!c||!r)return B2L_AKS_BAD_ARGUMENT;
    if(c->scalar_input_count>B2L_AKS_MAX_SCALARS||
       c->scalar_output_capacity>B2L_AKS_MAX_SCALARS||
       (c->scalar_input_count&&!c->scalar_input)||
       c->structure_input_size>B2L_AKS_MAX_STRUCTURE||
       (c->structure_input_size&&!c->structure_input))return B2L_AKS_BAD_ARGUMENT;
    if(s->closed)return B2L_AKS_NOT_OPEN;
    /* iOS 26 selector 0 is an empty IOConnectCallMethod after type-0 open. */
    if(c->selector==0){
        if(c->scalar_input_count||c->scalar_output_capacity||
           c->structure_input_size||c->structure_output_capacity)return B2L_AKS_BAD_ARGUMENT;
        s->opened=1;return B2L_AKS_SUCCESS;
    }
    if(!s->opened)return B2L_AKS_NOT_OPEN;
    switch(c->selector){
    case 14: /* aks_get_system: default ID omits the input scalar. */
        if(c->scalar_input_count>1||c->structure_input_size||c->structure_output_capacity)
            return B2L_AKS_BAD_ARGUMENT;
        if(c->scalar_input_count&&!system_handle(c->scalar_input[0]))return B2L_AKS_NOT_FOUND;
        if(c->scalar_output_capacity!=1)return B2L_AKS_BAD_ARGUMENT;
        r->scalars[0]=0;r->scalar_count=1;return B2L_AKS_SUCCESS;
    case 7: /* aks_get_lock_state: 1 handle -> 1 state scalar, no structures. */
        if(c->scalar_input_count!=1||c->scalar_output_capacity!=1||
           c->structure_input_size||c->structure_output_capacity)return B2L_AKS_BAD_ARGUMENT;
        if(!system_handle(c->scalar_input[0]))return B2L_AKS_NOT_FOUND;
        /* locked bits clear, unlocked-since-boot bit set (no passcode). */
        r->scalars[0]=4;r->scalar_count=1;return B2L_AKS_SUCCESS;
    default:
        /* Includes create/load/save/wrap/unwrap and unproved state selectors. */
        return B2L_AKS_UNSUPPORTED;
    }
}
