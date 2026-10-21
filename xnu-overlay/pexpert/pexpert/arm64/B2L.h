/* SPDX-License-Identifier: BSD-2-Clause
 * Exynos 9820 first-boot target. Generic ARMv8 instructions, no Apple PAC/BTI.
 * GICv2 and 26 MHz architected counter are described by the stock DTB.
 * First milestone deliberately starts only the firmware's boot CPU.
 */
#ifndef _PEXPERT_ARM64_B2L_H
#define _PEXPERT_ARM64_B2L_H
#define B2L                             1
#define B2L_BOOTSTRAP_TABLE_PAGES       10
/* Existing firmware DisplayReserved descriptor, unchanged. */
#define B2L_FB_RESERVED_BASE            0xCA000000ULL
#define B2L_FB_RESERVED_SIZE            0x01400000ULL
#define NO_MONITOR                      1
#define NO_ECORE                        1
#define ARM_ARCH_TIMER
#define __ARM_ARCH__                    8
#define __ARM_VMSA__                    8
#define __ARM_VFP__                     4
#define __ARM_COHERENT_CACHE__           1
#define __ARM_IC_NOALIAS_ICACHE__        1
#define __ARM_DEBUG__                   7
#define __ARM_ENABLE_SWAP__             1
#define __ARM_V8_CRYPTO_EXTENSIONS__     1
/* Exynos 9820 implements PAN. Exception entry may set PSTATE.PAN;
 * stock XNU copyio must temporarily clear it for checked EL0 accesses. */
#define __ARM_PAN_AVAILABLE__           1
#define HAS_GIC_V3                      0
#define B2L_TIMER_INTID                 27
#define B2L_GICD_PA                     0x10101000ULL
#define B2L_GICC_PA                     0x10102000ULL
#define B2L_COUNTER_HZ                  26000000ULL
#ifndef ASSEMBLER
#define PLATFORM_PANIC_LOG_DISABLED
#endif
#endif
