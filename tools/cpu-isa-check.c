/* SPDX-License-Identifier: MIT
 * Temporary Linux recovery process; no device or filesystem writes.
 * A zero exit status means the probes completed, not that PAC is supported.
 * Build with an AArch64 Linux libc/toolchain, e.g.:
 *   zig cc -target aarch64-linux-musl -static -O2 -Wall -Wextra -Werror \
 *       tools/cpu-isa-check.c -o /tmp/cpu-isa-check
 */
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <signal.h>
#include <setjmp.h>
#include <sched.h>
#include <sys/auxv.h>

static sigjmp_buf resume;

static void
fault(int sig)
{
    siglongjmp(resume, sig);
}

static int
read_isar1(uint64_t *out)
{
    int sig = sigsetjmp(resume, 1);
    if (sig) return sig;
    /* Linux may trap this access and supply a sanitized common feature set. */
    __asm__ volatile("mrs %0, id_aa64isar1_el1" : "=r"(*out));
    return 0;
}

static int
test_pacia(uint64_t *out)
{
    int sig = sigsetjmp(resume, 1);
    if (sig) return sig;
    /* Non-HINT PACIA x0,x1, verified by LLVM disassembly. No pointer is used. */
    __asm__ volatile("mov x0, #0x1000\n"
                     "mov x1, #0\n"
                     ".inst 0xdac10020\n"
                     "mov %0, x0"
                     : "=r"(*out) : : "x0", "x1");
    return 0;
}

static int
test_ldapr(uint64_t *out)
{
    static const uint64_t value = 0x1234;
    int sig = sigsetjmp(resume, 1);
    if (sig) return sig;
    __asm__ volatile(".arch_extension rcpc\nldapr %0, [%1]"
                     : "=r"(*out) : "r"(&value) : "memory");
    return 0;
}

int
main(void)
{
    struct sigaction sa = {0};
    sa.sa_handler = fault;
    if (sigemptyset(&sa.sa_mask) || sigaction(SIGILL, &sa, NULL) ||
        sigaction(SIGSEGV, &sa, NULL) || sigaction(SIGBUS, &sa, NULL)) {
        perror("sigaction");
        return 1;
    }
    printf("HWCAP=%016lx HWCAP2=%016lx\n",
           getauxval(AT_HWCAP), getauxval(AT_HWCAP2));
    cpu_set_t saved;
    if (sched_getaffinity(0, sizeof(saved), &saved)) {
        perror("getaffinity");
        return 1;
    }
    for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu) {
        if (!CPU_ISSET(cpu, &saved)) continue;
        cpu_set_t one;
        CPU_ZERO(&one);
        CPU_SET(cpu, &one);
        if (sched_setaffinity(0, sizeof(one), &one)) {
            perror("setaffinity");
            return 1;
        }
        uint64_t isar = 0, signed_ptr = 0, loaded = 0;
        int r = read_isar1(&isar);
        int p = test_pacia(&signed_ptr);
        int l = test_ldapr(&loaded);
        printf("cpu=%d ISAR1_signal=%d ISAR1=%016llx "
               "APA=%llu API=%llu GPA=%llu GPI=%llu "
               "PACIA_signal=%d result=%016llx "
               "LDAPR_signal=%d LDAPR_result=%016llx\n",
               cpu, r, (unsigned long long)isar,
               (unsigned long long)((isar >> 4) & 15),
               (unsigned long long)((isar >> 8) & 15),
               (unsigned long long)((isar >> 24) & 15),
               (unsigned long long)((isar >> 28) & 15),
               p, (unsigned long long)signed_ptr, l,
               (unsigned long long)loaded);
    }
    if (sched_setaffinity(0, sizeof(saved), &saved)) {
        perror("restoreaffinity");
        return 1;
    }
    return 0;
}
