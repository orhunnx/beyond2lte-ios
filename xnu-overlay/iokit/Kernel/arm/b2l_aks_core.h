/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef B2L_AKS_CORE_H
#define B2L_AKS_CORE_H
#include <stddef.h>
#include <stdint.h>
#define B2L_AKS_MAX_SCALARS 4u
#define B2L_AKS_MAX_STRUCTURE 256u
/* Volatile, no-passcode development state only. No SEP, encryption, key wrapping
 * or persistent keybag representation is implemented by this service. */
struct b2l_aks_client { uint8_t opened,closed; };
struct b2l_aks_call {
    uint32_t selector;
    const uint64_t *scalar_input;
    size_t scalar_input_count;
    const void *structure_input;
    size_t structure_input_size;
    size_t scalar_output_capacity,structure_output_capacity;
};
struct b2l_aks_reply {
    uint64_t scalars[B2L_AKS_MAX_SCALARS];
    size_t scalar_count;
    uint8_t structure[B2L_AKS_MAX_STRUCTURE];
    size_t structure_size;
};
enum b2l_aks_result {
    B2L_AKS_SUCCESS=0,B2L_AKS_UNSUPPORTED,B2L_AKS_BAD_ARGUMENT,
    B2L_AKS_NOT_OPEN,B2L_AKS_NOT_FOUND,B2L_AKS_NO_SPACE
};
void b2l_aks_client_init(struct b2l_aks_client *);
void b2l_aks_client_close(struct b2l_aks_client *);
enum b2l_aks_result b2l_aks_dispatch(struct b2l_aks_client *,const struct b2l_aks_call *,struct b2l_aks_reply *);
#endif
