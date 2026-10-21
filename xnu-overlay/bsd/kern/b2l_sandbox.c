/* SPDX-License-Identifier: BSD-2-Clause */
/* Built-in development policy for the explicitly unsigned B2L port.
 * This registers the name queried by libsystem_sandbox; it does not implement
 * production profile enforcement, container paths or extension tokens. */
#if defined(ARM64_BOARD_CONFIG_B2L) && CONFIG_MACF
#include <sys/types.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/kauth.h>
#include <sys/proc.h>
#include <sys/priv.h>
#include <sys/codesign.h>
#include <security/mac_policy.h>
#include <os/atomic_private.h>
#include <arm64/b2l_arm64e_compat.h>

void b2l_sandbox_register(void);

static _Atomic uint64_t b2l_sandbox_seen[2];
static _Atomic uint32_t b2l_sandbox_other_seen;
static _Atomic uint64_t b2l_sandbox_priv_seen;
static _Atomic uint32_t b2l_sandbox_other_priv_seen;
static bool b2l_sandbox_registered; /* only written by the BSD init thread */
static mac_policy_handle_t b2l_sandbox_handle;

static bool
b2l_sandbox_development_enabled(void)
{
    return b2l_arm64e_compat_enabled() && !cs_process_global_enforcement();
}

/* At most129 operation and65 privilege lines per boot, regardless of callers.
 * Only numeric API identifiers are logged; no task names, IDs or user data. */
static void
b2l_sandbox_log_operation(int operation)
{
    if (operation >= 0 && operation < 128) {
        unsigned index = (unsigned)operation / 64u;
        uint64_t bit = UINT64_C(1) << ((unsigned)operation % 64u);
        if (!(os_atomic_or_orig(&b2l_sandbox_seen[index], bit, relaxed) & bit)) {
            printf("B2L Sandbox: operation %d\n", operation);
        }
    } else if (!os_atomic_xchg(&b2l_sandbox_other_seen, 1u, relaxed)) {
        printf("B2L Sandbox: unrecognized operation range\n");
    }
}

/* Verified iOS26 op0x43 is two pointers to64-bit outputs. Keep the successful
 * zero-output contract, rather than the older A5 ENOENT behavior for this op.
 * A later copyout failure may follow an already completed earlier copyout,
 * matching normal copyio semantics; no fabricated nonzero outputs are used. */
static int
b2l_sandbox_container_expected(user_addr_t argument)
{
    struct { user64_addr_t expected; user64_addr_t restricted; } outputs;
    const uint64_t none = 0;
    int error = copyin(argument, &outputs, sizeof(outputs));
    if (error) return error;
    if (outputs.expected) {
        error = copyout(&none, outputs.expected, sizeof(none));
        if (error) return error;
    }
    if (outputs.restricted) {
        error = copyout(&none, outputs.restricted, sizeof(none));
    }
    return error;
}

static int
b2l_sandbox_syscall(struct proc *process, int operation, user_addr_t argument)
{
    /* Also gate callbacks, so an enforcement transition cannot leave this
     * provider permissive after its boot-time registration decision. */
    if (!b2l_sandbox_development_enabled() || !proc_is64bit(process)) {
        return ENOPOLICY;
    }
    b2l_sandbox_log_operation(operation);
    switch (operation) {
    case 4: /* container-path query: no fabricated path */
    case 5: /* extension issue: no fabricated token */
    case 6: /* extension consume: no fabricated token state */
        return ENOENT;
    case 0x43:
        return b2l_sandbox_container_expected(argument);
    default:
        /* Explicit unconfined development behavior, matching the A5 port. */
        return 0;
    }
}

static int
b2l_sandbox_priv_grant(kauth_cred_t credential __unused, int privilege)
{
    if (!b2l_sandbox_development_enabled()) return EPERM;
    if (privilege >= 1000 && privilege < 1064) {
        uint64_t bit = UINT64_C(1) << ((unsigned)privilege - 1000u);
        if (!(os_atomic_or_orig(&b2l_sandbox_priv_seen, bit, relaxed) & bit)) {
            printf("B2L Sandbox: privilege %d %s\n", privilege,
                privilege == PRIV_WORK_INTERVAL ? "granted" : "not granted");
        }
    } else if (!os_atomic_xchg(&b2l_sandbox_other_priv_seen, 1u, relaxed)) {
        printf("B2L Sandbox: other privilege not granted\n");
    }
    return privilege == PRIV_WORK_INTERVAL ? 0 : EPERM;
}

static const struct mac_policy_ops b2l_sandbox_ops = {
    .mpo_policy_syscall = b2l_sandbox_syscall,
    .mpo_priv_grant = b2l_sandbox_priv_grant,
};

static struct mac_policy_conf b2l_sandbox_conf = {
    .mpc_name = "Sandbox",
    .mpc_fullname = "B2L unsigned development Sandbox policy",
    .mpc_ops = &b2l_sandbox_ops,
};

/* Invoked immediately after mac_policy_initbsd(), before userspace exec.
 * No NOTLATE/UNLOADOK flag: it is a permanent built-in development provider.
 * A real preexisting Sandbox policy wins through mac_policy_register EEXIST. */
void
b2l_sandbox_register(void)
{
    if (!b2l_sandbox_development_enabled() || b2l_sandbox_registered) return;
    int error = mac_policy_register(&b2l_sandbox_conf, &b2l_sandbox_handle, NULL);
    if (!error) b2l_sandbox_registered = true;
    printf("B2L Sandbox: development provider %s (%d)\n",
        error ? "not registered" : "registered", error);
}
#endif /* ARM64_BOARD_CONFIG_B2L && CONFIG_MACF */
