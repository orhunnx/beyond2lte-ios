/* SPDX-License-Identifier: BSD-2-Clause
 * Build the pinned Apple OSS provider with its supported kext header view.
 * It owns the complete wait-element layout; XNU reserves equivalent storage.
 * Private BSD state is accessed through the existing XNU callback table.
 */
#ifndef B2L_PTHREAD_COMPAT_H
#define B2L_PTHREAD_COMPAT_H
#undef BSD_KERNEL_PRIVATE
#undef XNU_KERNEL_PRIVATE
#define __PTHREAD_EXPOSE_INTERNALS__ 1
#define NEEDS_SCHED_CALL_T 1
/* Preserve the provider's seven-argument exported vm_fault contract. */
#define vm_fault vm_fault_external
/* Separate the provider callback handle and helpers from XNU shim symbols. */
#define pthread_kern b2l_pthread_kern
#define current_uthread b2l_pthread_current_uthread
#define pthread_start b2l_pthread_start
#define pthread_stop b2l_pthread_stop
/* os/log expects the containing executable Mach-O header, not a fake kext. */
extern void *b2l_pthread_log_image __asm("__mh_execute_header");
#define __dso_handle b2l_pthread_log_image
#endif
