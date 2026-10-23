/* SPDX-License-Identifier: BSD-2-Clause */
/* Development-only service/connection provenance for native Mach IPC.
 * Included once by b2l_sandbox.c after its existing development-mode gate.
 * Native XNU owns rights/type checks and calls these hooks with owned labels. */
#ifndef B2L_MACH_FILTER_H
#define B2L_MACH_FILTER_H
#include <arm64/b2l_mach_filter.h>

enum b2l_mach_label_kind {
    B2L_MACH_NAMED_SERVICE = 1,
    B2L_MACH_ANONYMOUS_FORWARD = 2
};
struct b2l_mach_service_label {
    os_refcnt_t references;
    enum b2l_mach_label_kind kind;
};
/* Only successful ownership of the native callback table permits private
 * anonymous labels. A pre-existing real provider must never receive them. */
static bool b2l_mach_filter_owned;

static struct b2l_mach_service_label *
b2l_mach_label_create(enum b2l_mach_label_kind kind)
{
    struct b2l_mach_service_label *identity =
        kalloc_type(struct b2l_mach_service_label, Z_WAITOK | Z_ZERO);
    if (!identity) return NULL;
    os_ref_init(&identity->references, NULL);
    identity->kind = kind;
    return identity;
}

kern_return_t
b2l_mach_filter_anonymous_connection(uint32_t flags,uint32_t parent,void **out)
{
    if (!out) return KERN_INVALID_ARGUMENT;
    *out = NULL;
    if (!b2l_sandbox_development_enabled() ||
        !__atomic_load_n(&b2l_mach_filter_owned, __ATOMIC_ACQUIRE)) return KERN_SUCCESS;
    if ((flags & MPO_PORT_TYPE_MASK) != MPO_CONNECTION_PORT ||
        parent != MPO_ANONYMOUS_SERVICE || (flags & MPO_IMMOVABLE_RECEIVE))
        return KERN_INVALID_ARGUMENT;
    struct b2l_mach_service_label *identity = b2l_mach_label_create(B2L_MACH_ANONYMOUS_FORWARD);
    if (!identity) return KERN_RESOURCE_SHORTAGE;
    *out = identity;
    return KERN_SUCCESS;
}

/* This retain runs with a native port lock held: no allocation or sleep. */
static void
b2l_mach_filter_retain(void *label)
{
    if (label) os_ref_retain(&((struct b2l_mach_service_label *)label)->references);
}

static void
b2l_mach_filter_release(void *label)
{
    struct b2l_mach_service_label *identity = label;
    if (identity && os_ref_release(&identity->references) == 0) {
        kfree_type(struct b2l_mach_service_label, identity);
    }
}

static kern_return_t
b2l_mach_filter_alloc(mach_service_port_info_t info, void **out)
{
    if (!out) return KERN_INVALID_ARGUMENT;
    *out = NULL;
    if (!info) return KERN_INVALID_ARGUMENT;
    if (!b2l_sandbox_development_enabled()) return KERN_NOT_SUPPORTED;
    struct b2l_mach_service_label *identity = b2l_mach_label_create(B2L_MACH_NAMED_SERVICE);
    if (!identity) return KERN_RESOURCE_SHORTAGE;
    *out = identity;
    return KERN_SUCCESS;
}

/* XNU retained the service label for this callback. Transfer that reference
 * into the new connection label; its object identity is the parent relation.
 * Backward channels are intentionally left unlabeled by native XNU itself. */
static void *
b2l_mach_filter_derive(void *service, boolean_t *send_side_filtering)
{
    if (send_side_filtering) *send_side_filtering = FALSE;
    if (!service) return NULL;
    if (!send_side_filtering || !b2l_sandbox_development_enabled() ||
        ((struct b2l_mach_service_label *)service)->kind != B2L_MACH_NAMED_SERVICE) {
        b2l_mach_filter_release(service);
        return NULL;
    }
    return service;
}

/* XNU transfers one retained reference for each non-NULL input, including
 * two references when both pointers are equal. Consume both on every path.
 * KERN_INVALID_CAPABILITY is deliberately not used for a mismatch: libxpc
 * accepts that code as a legacy fallback without validating provenance. */
static kern_return_t
b2l_mach_filter_connection(void *service, void *connection, uint64_t *policy)
{
    if (policy) *policy = 0;
    kern_return_t result = KERN_INVALID_ARGUMENT;
    if (!b2l_sandbox_development_enabled()) {
        result = KERN_NOT_SUPPORTED;
    } else if (policy && service && connection) {
        struct b2l_mach_service_label *s = service, *c = connection;
        /* Native XNU already verified both receive-right port types. Named
         * connections preserve exact parent identity. An owned anonymous
         * forward label states its native anonymous kind, not a named parent. */
        if (s->kind == B2L_MACH_NAMED_SERVICE &&
            ((service == connection && c->kind == B2L_MACH_NAMED_SERVICE) ||
             c->kind == B2L_MACH_ANONYMOUS_FORWARD)) result = KERN_SUCCESS;
    }
    b2l_mach_filter_release(service);
    b2l_mach_filter_release(connection);
    return result;
}

/* ipc_policy.c retained this label before dropping the native port lock.
 * Consume that reference here. Explicit unconfined development behavior is
 * unchanged; native bootstrap domain-0 restrictions still precede this hook. */
static boolean_t
b2l_mach_filter_fetch(struct task *task __unused, void *label,
    mach_msg_id_t message __unused, mach_msg_filter_id *policy)
{
    boolean_t allowed = policy && label && b2l_sandbox_development_enabled();
    if (policy) *policy = MACH_MSG_FILTER_POLICY_ALLOW;
    b2l_mach_filter_release(label);
    return allowed;
}

static const struct mach_msg_filter_callbacks b2l_mach_filter_callbacks = {
    .version = MACH_MSG_FILTER_CALLBACKS_VERSION_1,
    .fetch_filter_policy = b2l_mach_filter_fetch,
    .alloc_service_port_sblabel = b2l_mach_filter_alloc,
    .dealloc_service_port_sblabel = b2l_mach_filter_release,
    .derive_sblabel_from_service_port = b2l_mach_filter_derive,
    .get_connection_port_filter_policy = b2l_mach_filter_connection,
    .retain_sblabel = b2l_mach_filter_retain,
};

static kern_return_t
b2l_mach_filter_register(void)
{
    if (!b2l_sandbox_development_enabled()) return KERN_NOT_SUPPORTED;
    kern_return_t result = mach_msg_filter_register_callback(&b2l_mach_filter_callbacks);
    if (result == KERN_SUCCESS)
        __atomic_store_n(&b2l_mach_filter_owned, true, __ATOMIC_RELEASE);
    return result; /* A repeated failed registration never clears ownership. */
}
#endif /* B2L_MACH_FILTER_H */
