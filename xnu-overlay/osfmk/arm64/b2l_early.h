/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef _ARM64_B2L_EARLY_H_
#define _ARM64_B2L_EARLY_H_
#include <pexpert/arm64/board_config.h>
#if defined(B2L) && !defined(ASSEMBLER)
#include <stdint.h>
#include <stdarg.h>
#include <pexpert/arm64/boot.h>
void b2l_early_init(const boot_args *args);
void b2l_early_putc(char c);
void b2l_early_mark(unsigned stage, const char *label);
void b2l_early_panic(const char *fmt, va_list *args);
void b2l_early_relocate(uint64_t base);
int b2l_early_range(uint64_t *physical, uint64_t *length);
int b2l_early_available(void);
#endif
#endif
