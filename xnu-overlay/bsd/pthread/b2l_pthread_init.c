/* SPDX-License-Identifier: BSD-2-Clause
 * Built-in Apple OSS libpthread-539.100.4, release-paired with XNU 12377.
 */
#if defined(ARM64_BOARD_CONFIG_B2L)
#include "b2l_pthread_compat.h"
#include "b2l_libpthread/kern/kern_init.c"
_Static_assert(sizeof(struct ksyn_waitq_element) == 56,
    "B2L kernel wait-element storage must match the real LP64 provider ABI");
_Static_assert(__alignof__(struct ksyn_waitq_element) == 8,
    "B2L provider wait-element alignment changed");
_Static_assert(offsetof(struct ksyn_waitq_element, kwe_uth) == 48,
    "B2L provider wait-element trailing uthread offset changed");
#define XNU_KERNEL_PRIVATE 1
#include <kern/startup.h>
#include <pexpert/pexpert.h>
__startup_func static void
b2l_pthread_register(void)
{
	kern_return_t kr = b2l_pthread_start(NULL, NULL);
	if (kr != KERN_SUCCESS || b2l_pthread_kern == NULL) {
		panic("B2L pthread provider registration failed: %d", kr);
	}
	kprintf("B2L pthread: libpthread-539.100.4 registered\n");
}
STARTUP(EARLY_BOOT, STARTUP_RANK_SECOND, b2l_pthread_register);
#endif
