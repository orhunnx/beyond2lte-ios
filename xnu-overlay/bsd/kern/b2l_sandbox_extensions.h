/* SPDX-License-Identifier: BSD-2-Clause */
/* Bookkeeping for the explicitly unconfined B2L development provider.
 * These are private B2L capabilities, not Apple sandbox signatures. They bind
 * an existing file class/path to a real issuer, optional target and consumer.
 * No VFS permissions, entitlements or code-signing state are changed here.
 * Caller gates this provider to unsigned B2L development mode. */
#define B2L_EXT_TOKENS 256u
#define B2L_EXT_HANDLES 1024u
#define B2L_EXT_PATH 1024u
#define B2L_EXT_TOKEN 73u
/* Exact file-extension classes from shipped ContainerManagerCommon plists and
 * the observed native tccd file request. Preserve each requested class; these
 * are not aliases and do not grant a TCC authorization decision. */
#define B2L_EXT_ADDRESSBOOK_CLASS 7u
#define B2L_EXT_PHOTOS_CLASS 8u
static const char *const b2l_ext_classes[] = {
    NULL, "com.apple.app-sandbox.read", "com.apple.app-sandbox.read-write",
    "com.apple.sandbox.container", "com.apple.sandbox.application-group",
    "com.apple.sandbox.system-container", "com.apple.sandbox.system-group",
    "com.apple.tcc.kTCCServiceAddressBook", "com.apple.tcc.kTCCServicePhotos"
};
static unsigned
b2l_ext_class(const char *name)
{
    for (unsigned i = 1; i < sizeof(b2l_ext_classes) / sizeof(b2l_ext_classes[0]); i++)
        if (!strcmp(name, b2l_ext_classes[i])) return i;
    return 0;
}
static bool
b2l_ext_root_class(unsigned class_id)
{
    return class_id == B2L_EXT_ADDRESSBOOK_CLASS || class_id == B2L_EXT_PHOTOS_CLASS;
}
static void
b2l_ext_issue_trace(proc_t process, const char *name, uint64_t type, uint64_t flags, int error)
{
    /* Separate known classes and unknown file/other-type requests, so repeated
     * AddressBook calls cannot hide a later Photos or unsupported-type error.
     * At most80 issuance lines with this class table; no paths or tokens. */
    enum { class_count = sizeof(b2l_ext_classes) / sizeof(b2l_ext_classes[0]) };
    static unsigned samples[class_count + 1][2];
    unsigned slot = type == 0 ? (name ? b2l_ext_class(name) : 0) : class_count;
    if (__atomic_fetch_add(&samples[slot][error != 0], 1, __ATOMIC_RELAXED) >= 4) return;
    char text[96];
    size_t n = name ? strlen(name) : 0;
    if (n >= sizeof(text)) n = sizeof(text) - 1;
    for (size_t i = 0; i < n; i++) text[i] = (unsigned char)name[i] >= 32 && (unsigned char)name[i] <= 126 ? name[i] : '?';
    text[n] = 0;
    b2l_diag_record("B2L EXT ISSUE: pid=%d class=%s type=%llu flags=0x%llx error=%d\n",
        proc_pid(process), name ? text : "-", (unsigned long long)type, (unsigned long long)flags, error);
}
struct b2l_ext_issue_args {
    uint64_t class_name, type, resource, flags, token, target_pid, target_version;
};
struct b2l_ext_consume_args { uint64_t token, length, handle; };
struct b2l_ext_release_args { uint64_t handle, reserved, path; };
struct b2l_ext_last_args { uint64_t handle, reserved, path, last; };
_Static_assert(sizeof(struct b2l_ext_issue_args) == 56, "iOS26 issue ABI");
_Static_assert(sizeof(struct b2l_ext_consume_args) == 24, "iOS26 consume ABI");
_Static_assert(sizeof(struct b2l_ext_release_args) == 24, "iOS26 release ABI");
_Static_assert(sizeof(struct b2l_ext_last_args) == 32, "iOS26 release-last ABI");
struct b2l_ext_token_entry {
    uint64_t issuer, target, serial;
    uint32_t refs, class_id;
    bool used, ready, writable, consumed;
    char token[B2L_EXT_TOKEN], path[B2L_EXT_PATH];
};
struct b2l_ext_handle_entry {
    uint64_t handle, owner, serial;
    unsigned token_index;
    bool used, ready;
};
static struct b2l_ext_token_entry b2l_ext_tokens[B2L_EXT_TOKENS];
static struct b2l_ext_handle_entry b2l_ext_handles[B2L_EXT_HANDLES];
static uint64_t b2l_ext_serial;
static LCK_GRP_DECLARE(b2l_ext_lock_group, "b2l_extensions");
static LCK_SPIN_DECLARE(b2l_ext_lock, &b2l_ext_lock_group);

static bool
b2l_ext_canonical(const char *path)
{
    if (path[0] != '/' || !path[1]) return false;
    const char *part = path + 1;
    for (const char *p = part;; p++) {
        if (*p && (unsigned char)*p < 32) return false;
        if (!*p || *p == '/') {
            size_t n = (size_t)(p - part);
            if (!n || (n == 1 && part[0] == '.') ||
                (n == 2 && part[0] == '.' && part[1] == '.')) return false;
            if (!*p) return true;
            part = p + 1;
        }
    }
}

/* Capture target lifetime identity outside the registry spin lock. */
static int
b2l_ext_target(const struct b2l_ext_issue_args *a, uint64_t *identity)
{
    *identity = 0;
    if (a->flags >> 18 || (a->flags & 0x20000u && !(a->flags & 0x10000u))) return EINVAL;
    if (!(a->flags & 0x10000u)) return (a->target_pid || a->target_version) ? EINVAL : 0;
    if (!a->target_pid || a->target_pid > INT32_MAX || a->target_version > UINT32_MAX) return EINVAL;
    proc_t target = proc_find((int)a->target_pid);
    if (!target) return ESRCH;
    int error = 0;
    if ((a->flags & 0x20000u) && proc_pidversion(target) != (uint32_t)a->target_version) error = ESRCH;
    if (!error) *identity = proc_uniqueid(target);
    proc_rele(target);
    return error;
}

static void
b2l_ext_drop_handle(unsigned index, bool retire)
{
    struct b2l_ext_handle_entry *h = &b2l_ext_handles[index];
    if (!h->used) return;
    struct b2l_ext_token_entry *t = &b2l_ext_tokens[h->token_index];
    if (t->used && t->serial == h->serial && t->refs) {
        t->refs--;
        if (!t->refs && (retire || t->consumed)) memset(t, 0, sizeof(*t));
    }
    memset(h, 0, sizeof(*h));
}

static int
b2l_ext_issue(proc_t process, user_addr_t argument)
{
    struct b2l_ext_issue_args a;
    char class_name[96], path[B2L_EXT_PATH], token[B2L_EXT_TOKEN];
    uint8_t random[32];
    size_t n = 0;
    int error = copyin(argument, &a, sizeof(a));
    if (error) return error;
    if (a.type != 0) {
        b2l_ext_issue_trace(process, NULL, a.type, a.flags, ENOTSUP);
        return ENOTSUP;
    }
    if (!a.class_name || !a.resource || !a.token) {
        b2l_ext_issue_trace(process, NULL, a.type, a.flags, EINVAL);
        return EINVAL;
    }
    error = copyinstr(a.class_name, class_name, sizeof(class_name), &n);
    if (error) {
        b2l_ext_issue_trace(process, NULL, a.type, a.flags, error);
        return error;
    }
    unsigned class_id = b2l_ext_class(class_name);
    if (!class_id) {
        b2l_ext_issue_trace(process, class_name, a.type, a.flags, ENOTSUP);
        return ENOTSUP;
    }
    bool writable = class_id != 1;
    error = copyinstr(a.resource, path, sizeof(path), &n);
    /* Native tccd issues these exact AddressBook/Photos file capabilities for
     * "/". Keep the root resource limited to those verified classes. */
    if (!error && !b2l_ext_canonical(path) &&
        !(b2l_ext_root_class(class_id) && !strcmp(path, "/"))) error = EINVAL;
    if (error) {
        b2l_ext_issue_trace(process, class_name, a.type, a.flags, error);
        return error;
    }
    uint64_t target;
    error = b2l_ext_target(&a, &target);
    if (error) {
        b2l_ext_issue_trace(process, class_name, a.type, a.flags, error);
        return error;
    }
    uint64_t issuer = proc_uniqueid(process);
    read_random(random, sizeof(random));
    memcpy(token, "b2l-dev-", 8);
    const char *hex = "0123456789abcdef";
    for (unsigned i = 0; i < sizeof(random); i++) {
        token[8 + 2 * i] = hex[random[i] >> 4];
        token[9 + 2 * i] = hex[random[i] & 15];
    }
    token[72] = 0;
    unsigned slot = B2L_EXT_TOKENS;
    uint64_t serial = 0;
    lck_spin_lock(&b2l_ext_lock);
    for (unsigned i = 0; i < B2L_EXT_TOKENS; i++) {
        if (b2l_ext_tokens[i].used && !strcmp(b2l_ext_tokens[i].token, token)) {
            lck_spin_unlock(&b2l_ext_lock);
            b2l_ext_issue_trace(process, class_name, a.type, a.flags, EAGAIN);
            return EAGAIN;
        }
        if (!b2l_ext_tokens[i].used && slot == B2L_EXT_TOKENS) slot = i;
    }
    if (slot != B2L_EXT_TOKENS) {
        struct b2l_ext_token_entry *t = &b2l_ext_tokens[slot];
        serial = ++b2l_ext_serial;
        if (!serial) serial = ++b2l_ext_serial;
        t->issuer = issuer; t->target = target; t->serial = serial;
        t->used = true; t->writable = writable; t->class_id = class_id;
        memcpy(t->token, token, sizeof(token));
        memcpy(t->path, path, strlen(path) + 1);
    }
    lck_spin_unlock(&b2l_ext_lock);
    if (slot == B2L_EXT_TOKENS) {
        b2l_ext_issue_trace(process, class_name, a.type, a.flags, ENOSPC);
        return ENOSPC;
    }
    error = copyout(token, a.token, sizeof(token));
    /* Exit notification can run between target lookup and publication.
     * Recheck after inserting the pending (not consumable) entry; a later
     * exit will see that entry, and an earlier exit is detected here. */
    if (!error && target) {
        uint64_t current_target = 0;
        error = b2l_ext_target(&a, &current_target);
        if (!error && current_target != target) error = ESRCH;
    }
    lck_spin_lock(&b2l_ext_lock);
    struct b2l_ext_token_entry *t = &b2l_ext_tokens[slot];
    if (t->used && t->serial == serial) {
        if (error) memset(t, 0, sizeof(*t));
        else t->ready = true;
    } else if (!error) error = ESRCH;
    lck_spin_unlock(&b2l_ext_lock);
    b2l_ext_issue_trace(process, class_name, a.type, a.flags, error);
    return error;
}

static int
b2l_ext_consume(proc_t process, user_addr_t argument)
{
    struct b2l_ext_consume_args a;
    char token[B2L_EXT_TOKEN];
    int error = copyin(argument, &a, sizeof(a));
    if (error) return error;
    if (!a.handle || a.length != sizeof(token) || !a.token) return EINVAL;
    error = copyin(a.token, token, sizeof(token));
    if (error) return error;
    if (token[72] || memcmp(token, "b2l-dev-", 8)) return EINVAL;
    for (unsigned i = 8; i < 72; i++) {
        if (!((token[i] >= '0' && token[i] <= '9') || (token[i] >= 'a' && token[i] <= 'f'))) return EINVAL;
    }
    uint64_t owner = proc_uniqueid(process), handle;
    read_random(&handle, sizeof(handle));
    handle &= INT64_MAX;
    if (!handle) return EAGAIN;
    unsigned ti = B2L_EXT_TOKENS, hi = B2L_EXT_HANDLES;
    uint64_t serial = 0;
    lck_spin_lock(&b2l_ext_lock);
    for (unsigned i = 0; i < B2L_EXT_HANDLES; i++) {
        if (b2l_ext_handles[i].used && b2l_ext_handles[i].handle == handle) {
            lck_spin_unlock(&b2l_ext_lock); return EAGAIN;
        }
        if (!b2l_ext_handles[i].used && hi == B2L_EXT_HANDLES) hi = i;
    }
    for (unsigned i = 0; i < B2L_EXT_TOKENS; i++) {
        struct b2l_ext_token_entry *t = &b2l_ext_tokens[i];
        if (t->used && t->ready && !strcmp(t->token, token)) { ti = i; break; }
    }
    if (ti == B2L_EXT_TOKENS) error = ENOENT;
    else if (b2l_ext_tokens[ti].target && b2l_ext_tokens[ti].target != owner) error = EPERM;
    else if (hi == B2L_EXT_HANDLES) error = ENOSPC;
    else {
        struct b2l_ext_token_entry *t = &b2l_ext_tokens[ti];
        serial = t->serial; t->refs++;
        b2l_ext_handles[hi] = (struct b2l_ext_handle_entry){handle, owner, serial, ti, true, false};
    }
    lck_spin_unlock(&b2l_ext_lock);
    if (error) return error;
    error = copyout(&handle, a.handle, sizeof(handle));
    lck_spin_lock(&b2l_ext_lock);
    struct b2l_ext_handle_entry *h = &b2l_ext_handles[hi];
    if (h->used && h->handle == handle && h->serial == serial) {
        if (error) b2l_ext_drop_handle(hi, false);
        else {
            h->ready = true;
            b2l_ext_tokens[h->token_index].consumed = true;
        }
    } else if (!error) error = ESRCH;
    lck_spin_unlock(&b2l_ext_lock);
    return error;
}

static int
b2l_ext_release(proc_t process, int operation, user_addr_t argument)
{
    struct b2l_ext_last_args a = {0};
    int error = copyin(argument, &a, operation == 0x50 ? sizeof(a) : sizeof(struct b2l_ext_release_args));
    if (error) return error;
    if (a.reserved || (a.handle && a.path) || (!a.handle && !a.path)) return EINVAL;
    /* The distinct release-last ABI is explicitly unsupported. */
    if (operation == 0x50) return ENOTSUP;
    char path[B2L_EXT_PATH]; size_t n;
    if (a.path) {
        error = copyinstr(a.path, path, sizeof(path), &n);
        if (error) return error;
        if (!b2l_ext_canonical(path) && strcmp(path, "/")) return EINVAL;
    }
    uint64_t owner = proc_uniqueid(process);
    error = ENOENT;
    lck_spin_lock(&b2l_ext_lock);
    for (unsigned i = 0; i < B2L_EXT_HANDLES; i++) {
        struct b2l_ext_handle_entry *h = &b2l_ext_handles[i];
        if (!h->used || !h->ready || h->owner != owner) continue;
        struct b2l_ext_token_entry *t = &b2l_ext_tokens[h->token_index];
        if (a.path && !strcmp(path, "/") && !b2l_ext_root_class(t->class_id)) continue;
        if ((a.handle && h->handle == a.handle) || (a.path && !strcmp(t->path, path))) {
            b2l_ext_drop_handle(i, true); error = 0;
            if (a.handle) break;
        }
    }
    lck_spin_unlock(&b2l_ext_lock);
    return error;
}

static int
b2l_extension_syscall(proc_t process, int operation, user_addr_t argument)
{
    if (!b2l_sandbox_development_enabled() || !process || !proc_is64bit(process)) return ENOPOLICY;
    int error;
    switch (operation) {
    case 5: return b2l_ext_issue(process, argument);
    case 6: error = b2l_ext_consume(process, argument); break;
    case 7: case 0x50: error = b2l_ext_release(process, operation, argument); break;
    default: return ENOTSUP;
    }
    static unsigned samples[2][2];
    if (__atomic_fetch_add(&samples[operation != 6][error != 0], 1, __ATOMIC_RELAXED) < 8)
        b2l_diag_record("B2L EXT RESULT: pid=%d op=%d error=%d\n", proc_pid(process), operation, error);
    return error;
}

static void
b2l_extension_process_exit(proc_t process)
{
    uint64_t identity = proc_uniqueid(process);
    lck_spin_lock(&b2l_ext_lock);
    for (unsigned i = 0; i < B2L_EXT_HANDLES; i++) {
        struct b2l_ext_handle_entry *h = &b2l_ext_handles[i];
        if (h->used && h->owner == identity) b2l_ext_drop_handle(i, true);
    }
    for (unsigned i = 0; i < B2L_EXT_TOKENS; i++) {
        struct b2l_ext_token_entry *t = &b2l_ext_tokens[i];
        if (t->used && (t->target == identity || (!t->refs && t->issuer == identity))) {
            memset(t, 0, sizeof(*t));
        }
    }
    lck_spin_unlock(&b2l_ext_lock);
}
