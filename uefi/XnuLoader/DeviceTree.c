/* SPDX-License-Identifier: BSD-2-Clause */
#include "DeviceTree.h"
#include "b2l_secondary.h"
#include "b2l_cpu_topology.h"
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
    struct node root, chosen, memory_map, manifest, product, buttons, defaults, memory, cpus, cpu, arm_io, gic, aliases;
    b2l_u32 two = 2, one = 1, zero = 0, cpu_reg;
    b2l_u64 ranges[] = { 0, 0x10000000ULL, 0x20000000ULL };
    b2l_u64 gic_regs[] = { 0x101000, 0x1000, 0x102000, 0x1000 };
    b2l_u64 mem_regs[2];
    unsigned cpu_id;
    static const char *const cpu_names[] = {"cpu0","cpu1","cpu2","cpu3","cpu4","cpu5","cpu6","cpu7"};
    if (!buffer || !c || !used || !c->counter_frequency || capacity < 8 ||
        (c->has_boot_ufs_lun && c->boot_ufs_lun > 7) ||
        (c->has_wifi_mac && !b2l_wifi_mac_valid(c->wifi_mac))) return -1;
    cpu_reg = (b2l_u32)(c->mpidr & 0x0000ffffULL);
    /* This isolated SMP target only supports the proven boot MPIDR 0. */
    if (cpu_reg != 0) return -1;
    if ((c->secondary_base || c->secondary_size) &&
        (!b2l_secondary_address(c->secondary_base) || c->secondary_size!=B2L_SECONDARY_BYTES))return -1;
    if(c->ram_count&&b2l_ram_validate(c->ram,c->ram_count,c->memory_base,c->memory_size))return -1;
    mem_regs[0] = c->memory_base; mem_regs[1] = c->memory_size;
    root = begin(&w, 0, "device-tree");
    /* hw.product/hw.machine feed MobileGestalt ProductType and DeviceClass.
     * Keep Samsung compatible for the actual B2L driver match. */
    text_prop(&w, &root, "model", "iPhone17,2");
    /* UIKit builds EventTimingProfiles/<hw.targettype>.<kind>.plist.
     * Select the D94 resources actually shipped in this source image. The
     * Samsung driver-compatible property and physical hardware stay separate. */
    text_prop(&w, &root, "target-type", "D94");
    text_prop(&w, &root, "compatible", "samsung,beyond2lte");
    prop(&w, &root, "#address-cells", &two, 4);
    prop(&w, &root, "#size-cells", &two, 4);
    chosen = begin(&w, &root, "chosen");
    prop(&w, &chosen, "random-seed", c->random_seed, sizeof(c->random_seed));
    if(c->has_wifi_mac)
        prop(&w, &chosen, "mac-address-wifi0", c->wifi_mac, B2L_WIFI_MAC_BYTES);
    prop(&w, &chosen, "dram-base", &c->memory_base, 8);
    prop(&w, &chosen, "dram-size", &c->memory_size, 8);
    if(c->ram_count)prop(&w,&chosen,"b2l,owned-ram-v1",c->ram,c->ram_count*sizeof(c->ram[0]));
    prop(&w, &chosen, "debug-enabled", &one, 4);
    text_prop(&w, &chosen, "crypto-hash-method", "sha2-384");
    b2l_u32 display_scale = 3;
    prop(&w, &chosen, "display-scale", &display_scale, 4);
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
    if(c->native_cache_size){
        b2l_u64 cache_range[2]={c->native_cache_base,c->native_cache_size};
        prop(&w,&chosen,"b2l,native-cache",cache_range,sizeof(cache_range));
    }
    if(c->secondary_size){
        b2l_u64 range[2]={c->secondary_base,c->secondary_size};
        prop(&w,&chosen,"b2l,secondary-trampoline",range,sizeof(range));
    }
    if(c->touch_status[0]==1)prop(&w,&chosen,"b2l,touch-status",c->touch_status,sizeof(c->touch_status));
    text_prop(&w, &chosen, "firmware-version", "beyond2lte-ios EFI loader");
    text_prop(&w, &chosen, "stdout-path", "/framebuffer");
    /* XNU requires this container even without a TrustCache or AuxKC.
     * Omit range properties unless an actual corresponding payload exists. */
    memory_map = begin(&w, &chosen, "memory-map");
    end(&w, &memory_map);
    /* libimage4 opens this registry container independently of its properties.
     * No Apple manifest, signed chip identity or trust value is fabricated. */
    manifest = begin(&w, &chosen, "manifest-properties");
    end(&w, &manifest);
    end(&w, &chosen);
    /* The board has a product node even though it is not an Apple product. */
    product = begin(&w, &root, "product");
    text_prop(&w, &product, "product-name", "iPhone 16 Pro Max");
    text_prop(&w, &product, "model", "iPhone17,2");
    text_prop(&w, &product, "board-config", "beyond2lte");
    /* Native MobileGestalt reads artwork traits from /product, independently
     * of hw.machine. Product/idiom/subtype/scale follow the source d94ap
     * asset profile; the physical framebuffer geometry remains c->video.
     * Our BGRA CPU path is sRGB and has no Apple Metal device. Select only
     * the source profile's oldest artwork fallback, not APPLE9 assets.
     * Artwork tags do not register a GPU or advertise API availability. */
    b2l_u32 artwork_subtype = 2868, artwork_scale = 3;
    text_prop(&w, &product, "product-description", "iPhone 16 Pro Max");
    text_prop(&w, &product, "artwork-device-idiom", "phone");
    prop(&w, &product, "artwork-device-subtype", &artwork_subtype, sizeof(artwork_subtype));
    prop(&w, &product, "artwork-scale-factor", &artwork_scale, sizeof(artwork_scale));
    text_prop(&w, &product, "artwork-display-gamut", "sRGB");
    text_prop(&w, &product, "graphics-featureset-class", "GLES2,0");
    text_prop(&w, &product, "graphics-featureset-fallbacks", "GLES2,0");
    end(&w, &product);
    /* Native MG HomeButtonType reads this four-byte CFData as a number.
     * Absent metadata defaults to mechanical Home (0). This device has none. */
    buttons = begin(&w, &root, "buttons");
    prop(&w, &buttons, "home-button-type", &two, sizeof(two));
    end(&w, &buttons);
    /* pmap requires /defaults; absent tuning/range properties retain its
     * built-in defaults instead of claiming nonexistent protected regions. */
    defaults = begin(&w, &root, "defaults");
    /* Native PM must not enter CPU_OFF/S2R before the board has a resume path.
     * Display blanking and ordinary interrupt-driven CPU idle remain separate. */
    prop(&w, &defaults, "sleep-disabled", &one, sizeof(one));
    end(&w, &defaults);
    memory = begin(&w, &root, "memory");
    text_prop(&w, &memory, "device_type", "memory");
    if(c->ram_count)prop(&w,&memory,"reg",c->ram,c->ram_count*sizeof(c->ram[0]));
    else prop(&w, &memory, "reg", mem_regs, sizeof(mem_regs));
    end(&w, &memory);
    cpus = begin(&w, &root, "cpus");
    prop(&w, &cpus, "#address-cells", &one, 4);
    prop(&w, &cpus, "#size-cells", &zero, 4);
    for (cpu_id = 0; cpu_id < 8; ++cpu_id) {
        cpu_reg = cpu_id < 6 ? cpu_id : 0x100 + cpu_id - 6;
        cpu = begin(&w, &cpus, cpu_names[cpu_id]);
        text_prop(&w, &cpu, "device_type", "cpu");
        text_prop(&w, &cpu, "compatible", "b2l,boot-cpu");
        text_prop(&w, &cpu, "state", cpu_id ? "stopped" : "running");
        prop(&w, &cpu, "reg", &cpu_reg, 4);
        prop(&w, &cpu, "cpu-id", &cpu_reg, 4);
        b2l_u32 cluster_id=b2l_cpu_cluster(cpu_id);
        b2l_u32 cluster_type=b2l_cpu_cluster_type(cpu_id);
        prop(&w, &cpu, "cluster-id", &cluster_id, 4);
        prop(&w, &cpu, "die-cluster-id", &cluster_id, 4);
        b2l_u32 logical_core=b2l_cpu_cluster_core(cpu_id);
        prop(&w, &cpu, "cluster-core-id", &logical_core, 4);
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
    if (c->video.base) {
        struct node display = begin(&w, &arm_io, "disp0");
        text_prop(&w, &display, "compatible", "samsung,beyond2lte-display");
        /* Native IOMobileFramebuffer counts only display-subsystem DT children. */
        text_prop(&w, &display, "device_type", "display-subsystem");
        b2l_u64 display_regs[] = {c->video.base, c->video.row_bytes * c->video.height};
        prop(&w, &display, "reg", display_regs, sizeof(display_regs));
        end(&w, &display);
    }
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
