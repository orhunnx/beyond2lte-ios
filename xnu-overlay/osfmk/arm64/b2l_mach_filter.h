/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef B2L_MACH_FILTER_API_H
#define B2L_MACH_FILTER_API_H
#include <stdint.h>
#include <mach/kern_return.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Called only outside port locks, before native ipc_port_alloc takes ownership.
 * Inactive B2L provider is a native pass-through with a NULL output label. */
kern_return_t b2l_mach_filter_anonymous_connection(uint32_t flags,uint32_t parent,void **out);
#ifdef __cplusplus
}
#endif
#endif
