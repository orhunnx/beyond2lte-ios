/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef B2L_DIAG_H
#define B2L_DIAG_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void b2l_diag_record(const char*,...) __attribute__((format(printf,1,2)));
void b2l_diag_wait(const char*,uint64_t,uint64_t,uint64_t);
void b2l_diag_ipc(int,uint64_t,uint32_t,uint32_t,int32_t,uint32_t,uint32_t,uint32_t);
void b2l_diag_syscall(unsigned,int,const uint64_t*);
void b2l_diag_service(const char*,const char*,uint64_t);
#ifdef __cplusplus
}
#endif
#endif
