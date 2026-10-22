/* SPDX-License-Identifier: BSD-2-Clause */
#include "DeviceTree.h"
struct writer { unsigned char *buf; b2l_size size, pos; int error; };
struct node { b2l_size offset; b2l_u32 properties, children; };
static void bytes(struct writer *w, const void *data, b2l_size length)
{
    b2l_size i;
    const unsigned char *p = data;
    if (w->error || length > w->size - w->pos) { w->error = 1; return; }
    for (i = 0; i < length; ++i) w->buf[w->pos++] = p ? p[i] : 0;
}
static void u32(struct writer *w, b2l_u32 value) { bytes(w, &value, sizeof(value)); }
static b2l_size string_size(const char *s)
{
    b2l_size n = 0;
    while (s[n]) ++n;
    return n + 1;
}
static void prop(struct writer *w, struct node *n, const char *name,
                 const void *data, b2l_u32 size)
{
    b2l_size length = string_size(name);
    if (length > 32) { w->error = 1; return; }
    bytes(w, name, length); bytes(w, 0, 32 - length);
    u32(w, size); bytes(w, data, size); bytes(w, 0, (4 - (size & 3)) & 3);
    ++n->properties;
}
static void text_prop(struct writer *w, struct node *n, const char *key, const char *value)
{ prop(w, n, key, value, (b2l_u32)string_size(value)); }
static struct node begin(struct writer *w, struct node *parent, const char *name)
{
    struct node n = { w->pos, 0, 0 };
    if (parent) ++parent->children;
    u32(w, 0); u32(w, 0);
    text_prop(w, &n, "name", name);
    return n;
}
static void end(struct writer *w, const struct node *n)
{
    b2l_size saved = w->pos;
    if (w->error) return;
    w->pos = n->offset; u32(w, n->properties); u32(w, n->children); w->pos = saved;
}
int b2l_build_device_tree(void *buffer, b2l_size capacity,
                         const struct b2l_dt_config *c, b2l_size *used)
{
    struct writer w = { buffer, capacity, 0, 0 };
    struct node root, chosen, memory_map, defaults, memory, cpus, cpu, arm_io, gic, aliases;
    b2l_u32 two = 2, one = 1, zero = 0, cpu_reg, cluster_type = 'S';
    b2l_u64 ranges[] = { 0, 0x10000000ULL, 0x20000000ULL };
    b2l_u64 gic_regs[] = { 0x101000, 0x1000, 0x102000, 0x1000 };
    b2l_u64 mem_regs[2];
    unsigned cpu_id;
    if (!buffer || !c || !used || !c->counter_frequency || capacity < 8 ||
        (c->has_boot_ufs_lun && c->boot_ufs_lun > 7)) return -1;
    cpu_reg = (b2l_u32)(c->mpidr & 0x0000ffffULL);
    /* This isolated SMP target only supports the proven boot MPIDR 0. */
    if (cpu_reg != 0) return -1;
    mem_regs[0] = c->memory_base; mem_regs[1] = c->memory_size;
    root = begin(&w, 0, "device-tree");
    text_prop(&w, &root, "model", "Samsung Galaxy S10+ (beyond2lte)");
    text_prop(&w, &root, "compatible", "samsung,beyond2lte");
    prop(&w, &root, "#address-cells", &two, 4);
    prop(&w, &root, "#size-cells", &two, 4);
    chosen = begin(&w, &root, "chosen");
    prop(&w, &chosen, "random-seed", c->random_seed, sizeof(c->random_seed));
    prop(&w, &chosen, "dram-base", &c->memory_base, 8);
    prop(&w, &chosen, "dram-size", &c->memory_size, 8);
    prop(&w, &chosen, "debug-enabled", &one, 4);
    /* Development-board tuple consumed by mobileactivationd's native local
     * Hactivation path. No activation record or device identity is supplied.
     * Verified against the iOS 26.6.2 DeviceType initializer. */
    prop(&w, &chosen, "certificate-production-status", &zero, 4);
    prop(&w, &chosen, "effective-production-status-ap", &zero, 4);
    prop(&w, &chosen, "certificate-security-mode", &one, 4);
    prop(&w, &chosen, "effective-security-mode-sep", &one, 4);
    /* Exynos does not provide Apple's CTRR. XNU requires an explicit native
     * int policy value, including zero, to initialize ml_unsafe_kernel_text. */
    prop(&w, &chosen, "kernel-ctrr-to-be-enabled", &zero, sizeof(zero));
    if(c->has_boot_ufs_lun)
        prop(&w, &chosen, "b2l,boot-ufs-lun", &c->boot_ufs_lun, sizeof(c->boot_ufs_lun));
    text_prop(&w, &chosen, "firmware-version", "beyond2lte-ios EFI loader");
    text_prop(&w, &chosen, "stdout-path", "/framebuffer");
    /* XNU requires this container even without a TrustCache or AuxKC.
     * Omit range properties unless an actual corresponding payload exists. */
    memory_map = begin(&w, &chosen, "memory-map");
    end(&w, &memory_map);
    end(&w, &chosen);
    /* pmap requires /defaults; absent tuning/range properties retain its
     * built-in defaults instead of claiming nonexistent protected regions. */
    defaults = begin(&w, &root, "defaults");
    end(&w, &defaults);
    memory = begin(&w, &root, "memory");
    text_prop(&w, &memory, "device_type", "memory");
    prop(&w, &memory, "reg", mem_regs, sizeof(mem_regs));
    end(&w, &memory);
    cpus = begin(&w, &root, "cpus");
    prop(&w, &cpus, "#address-cells", &one, 4);
    prop(&w, &cpus, "#size-cells", &zero, 4);
    for (cpu_id = 0; cpu_id < 2; ++cpu_id) {
        cpu_reg = cpu_id;
        cpu = begin(&w, &cpus, cpu_id ? "cpu1" : "cpu0");
        text_prop(&w, &cpu, "device_type", "cpu");
        text_prop(&w, &cpu, "compatible", "b2l,boot-cpu");
        text_prop(&w, &cpu, "state", cpu_id ? "stopped" : "running");
        prop(&w, &cpu, "reg", &cpu_reg, 4);
        prop(&w, &cpu, "cpu-id", &cpu_reg, 4);
        prop(&w, &cpu, "cluster-id", &zero, 4);
        prop(&w, &cpu, "cluster-type", &cluster_type, 4);
        prop(&w, &cpu, "timebase-frequency", &c->counter_frequency, 4);
        prop(&w, &cpu, "fixed-frequency", &c->counter_frequency, 4);
        end(&w, &cpu);
    }
    end(&w, &cpus);
    arm_io = begin(&w, &root, "arm-io");
    text_prop(&w, &arm_io, "device_type", "soc");
    text_prop(&w, &arm_io, "compatible", "samsung,exynos9820");
    prop(&w, &arm_io, "#address-cells", &two, 4);
    prop(&w, &arm_io, "#size-cells", &two, 4);
    prop(&w, &arm_io, "ranges", ranges, sizeof(ranges));
    gic = begin(&w, &arm_io, "gic");
    text_prop(&w, &gic, "compatible", "arm,gic-400");
    text_prop(&w, &gic, "interrupt-controller", "master");
    prop(&w, &gic, "reg", gic_regs, sizeof(gic_regs));
    end(&w, &gic);
    end(&w, &arm_io);
    if (c->video.base) {
        struct node fb = begin(&w, &root, "framebuffer");
        b2l_u64 fb_regs[] = { c->video.base, c->video.row_bytes * c->video.height };
        text_prop(&w, &fb, "device_type", "display");
        text_prop(&w, &fb, "compatible", "simple-framebuffer");
        prop(&w, &fb, "reg", fb_regs, sizeof(fb_regs));
        prop(&w, &fb, "width", &c->video.width, 8);
        prop(&w, &fb, "height", &c->video.height, 8);
        prop(&w, &fb, "stride", &c->video.row_bytes, 8);
        text_prop(&w, &fb, "format", "x8r8g8b8");
        end(&w, &fb);
    }
    aliases = begin(&w, &root, "aliases");
    text_prop(&w, &aliases, "display0", "/framebuffer");
    end(&w, &aliases); end(&w, &root);
    if (w.error) return -2;
    *used = w.pos;
    return 0;
}
