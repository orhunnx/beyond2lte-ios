/* SPDX-License-Identifier: BSD-2-Clause */
/* Unavailable response only for iOS 26 AMFI operation 0x66. No predicate,
 * identity, signature or entitlement is evaluated or accepted here. */
#ifndef B2L_AMFI_CONSTRAINT_H
#define B2L_AMFI_CONSTRAINT_H
#define B2L_AMFI_CONSTRAINT_MAX 0x4000u
#define B2L_AMFI_CONSTRAINT_RESULT_SIZE 0x404u
/* Observed stock AMFI result for unsupported running-process constraints. */
#define B2L_AMFI_CONSTRAINT_UNSUPPORTED 3u
struct b2l_amfi_constraint_args {
    uint32_t audit_token[8];
    user64_addr_t constraint;
    uint32_t constraint_size;
    uint32_t reserved0;
    user64_addr_t result;
    uint32_t result_size;
    uint32_t reserved1;
};
struct b2l_amfi_constraint_result {
    uint32_t error_code;
    char description[1024];
};
_Static_assert(sizeof(struct b2l_amfi_constraint_args) == 64, "AMFI argument ABI");
_Static_assert(__builtin_offsetof(struct b2l_amfi_constraint_args, constraint) == 0x20, "AMFI constraint ABI");
_Static_assert(__builtin_offsetof(struct b2l_amfi_constraint_args, result) == 0x30, "AMFI result pointer ABI");
_Static_assert(sizeof(struct b2l_amfi_constraint_result) == B2L_AMFI_CONSTRAINT_RESULT_SIZE, "AMFI result ABI");

/* The stock syscall copies its diagnostic result on input/evaluation errors
 * once a valid result pointer/capacity is known. Preserve that dual contract. */
static int
b2l_amfi_constraint_reply(user64_addr_t output, uint32_t code,
    const char *description, int status)
{
    struct b2l_amfi_constraint_result result;
    bzero(&result, sizeof(result));
    result.error_code = code;
    strlcpy(result.description, description, sizeof(result.description));
    int error = copyout(&result, output, sizeof(result));
    return error ? error : status;
}

static int
b2l_amfi_constraint_syscall(struct proc *process, int operation, user_addr_t argument)
{
    if (!b2l_amfi_active() || !proc_is64bit(process)) return ENOPOLICY;
    if (operation != 0x66) return ENOTSUP;
    struct b2l_amfi_constraint_args args;
    int error = copyin(argument, &args, sizeof(args));
    if (error) return error;
    if (!args.result || args.result_size != B2L_AMFI_CONSTRAINT_RESULT_SIZE) return EINVAL;
    if (!args.constraint || !args.constraint_size ||
        args.constraint_size > B2L_AMFI_CONSTRAINT_MAX) {
        return b2l_amfi_constraint_reply(args.result, 5,
            "Invalid launch constraint buffer or length; not evaluated", EINVAL);
    }
    if (args.constraint > UINT64_MAX - (args.constraint_size - 1u)) {
        return b2l_amfi_constraint_reply(args.result, 5,
            "Unreadable launch constraint range; not evaluated", EFAULT);
    }
    /* Validate the readable input range without interpreting its bytes.
     * Fixed chunks bound stack usage independently of user-controlled sizes. */
    uint8_t bytes[64];
    for (uint32_t offset = 0; offset < args.constraint_size;) {
        uint32_t size = args.constraint_size - offset;
        if (size > sizeof(bytes)) size = sizeof(bytes);
        error = copyin(args.constraint + offset, bytes, size);
        if (error) {
            return b2l_amfi_constraint_reply(args.result, 5,
                "Failed to read launch constraint; not evaluated", error);
        }
        offset += size;
    }
    return b2l_amfi_constraint_reply(args.result, B2L_AMFI_CONSTRAINT_UNSUPPORTED,
        "Launch constraint matching unavailable in B2L development mode; not evaluated",
        ENOTSUP);
}
static const struct mac_policy_ops b2l_amfi_constraint_ops = {
    .mpo_policy_syscall = b2l_amfi_constraint_syscall,
};
static struct mac_policy_conf b2l_amfi_constraint_conf = {
    .mpc_name = "AMFI",
    .mpc_fullname = "B2L unavailable launch constraint matching API",
    .mpc_ops = &b2l_amfi_constraint_ops,
};
static mac_policy_handle_t b2l_amfi_constraint_handle;
static bool b2l_amfi_constraint_registered;
static void
b2l_amfi_constraint_register(void)
{
    if (!b2l_amfi_active() || b2l_amfi_constraint_registered) return;
    int error = mac_policy_register(&b2l_amfi_constraint_conf,
        &b2l_amfi_constraint_handle, NULL);
    if (!error) b2l_amfi_constraint_registered = true;
    printf("B2L AMFI: unavailable constraint API %s (%d)\n",
        error ? "not registered" : "registered", error);
}
#endif /* B2L_AMFI_CONSTRAINT_H */
