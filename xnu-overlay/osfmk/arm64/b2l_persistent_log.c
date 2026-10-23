/* SPDX-License-Identifier: BSD-2-Clause */
#include "b2l_persistent_log.h"
#include <arm64/b2l_arm64e_compat.h>
#include <arm64/b2l_early.h>
#include <arm64/b2l_diag.h>
#include <kern/thread.h>
#include <kern/sched_prim.h>
#include <kern/clock.h>
#include <mach/mach_time.h>
static struct b2l_log_ring console_ring;
static struct b2l_log_writer disk_writer;
static uint32_t start_state;
static uint64_t flush_failures;
static int last_error;
void b2l_log_append(char c){b2l_log_ring_append(&console_ring,(uint8_t)c);}
void b2l_log_append_bytes(const void*p,size_t n){b2l_log_ring_append_bytes(&console_ring,p,n);}
static void b2l_log_worker(void*argument __unused,wait_result_t result __unused)
{
    unsigned reports=0;
    for(;;){
        int err=b2l_log_writer_drain(&disk_writer,&console_ring,128);
        if(err){__atomic_fetch_add(&flush_failures,1,__ATOMIC_RELAXED);__atomic_store_n(&last_error,err,__ATOMIC_RELAXED);}
        /* Outside every storage lock/callback. One bounded progress line per
         * ~30s distinguishes an awake scheduler from silent EL0 execution. */
        if(++reports==6){
            uint64_t v[4];b2l_arm64e_compat_snapshot(v);reports=0;
            b2l_diag_progress();
            uint64_t ns=0;absolutetime_to_nanoseconds(mach_absolute_time(),&ns);
            b2l_early_trace("B2L LIVE: traps=%llu done=%llu pid=%llu pc=0x%llx flush_errors=%llu last_error=%d sec=%llu\n",
                v[0],v[1],v[2],v[3],__atomic_load_n(&flush_failures,__ATOMIC_RELAXED),__atomic_load_n(&last_error,__ATOMIC_RELAXED),ns/1000000000);
        }
        assert_wait_timeout((event_t)&disk_writer,THREAD_UNINT,5,NSEC_PER_SEC);
        thread_block(THREAD_CONTINUE_NULL);
    }
}
int b2l_log_start(const struct b2l_log_sink*sink)
{
    uint32_t expected=0;
    if(!__atomic_compare_exchange_n(&start_state,&expected,1,0,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED))return B2L_LOG_BAD_ARGUMENT;
    int err=b2l_log_writer_open(&disk_writer,sink);
    if(err){__atomic_store_n(&start_state,0,__ATOMIC_RELEASE);return err;}
    thread_t worker; kern_return_t kr=kernel_thread_start(b2l_log_worker,0,&worker);
    if(kr!=KERN_SUCCESS){disk_writer.active=0;__atomic_store_n(&start_state,0,__ATOMIC_RELEASE);return (int)kr;}
    thread_deallocate(worker);__atomic_store_n(&start_state,2,__ATOMIC_RELEASE);return 0;
}
