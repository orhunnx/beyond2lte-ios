/* SPDX-License-Identifier: BSD-2-Clause
 * Allocation-free XNU-owned GOP text console, following the A5 port's early
 * console lifecycle, with geometry from boot_args instead of board constants.
 * The physical GOP alias is established in start.s. arm_vm_init supplies a
 * reserved TTBR1 alias before removing TTBR0; the serial sink uses it thereafter.
 */
#include <arm64/b2l_early.h>
#include <arm64/b2l_persistent_log.h>
#if defined(B2L)
#include <kern/misc_protos.h>
extern unsigned char iso_font[256 * 16];
static volatile uint32_t *b2l_fb;
static uint64_t b2l_fb_physical, b2l_fb_length;
static unsigned b2l_width, b2l_height, b2l_stride, b2l_scale;
static unsigned b2l_cols, b2l_rows, b2l_row, b2l_col;
static unsigned b2l_gui;
void b2l_early_gui_present(void){__atomic_store_n(&b2l_gui,1,__ATOMIC_RELEASE);}
static unsigned b2l_active, b2l_panic_owner, b2l_panic_floor;
static unsigned b2l_console_cpu(void)
{
    return (unsigned)(__builtin_arm_rsr64("MPIDR_EL1") & 0xffff) + 1;
}
extern void b2l_smp_dump(void);

static void
b2l_clear_row(unsigned row)
{
	for (unsigned y = row * 16 * b2l_scale; y < (row + 1) * 16 * b2l_scale; ++y) {
		volatile uint32_t *line = b2l_fb + (uint64_t)y * b2l_stride / 4;
		for (unsigned x = 0; x < b2l_width; ++x) {
			line[x] = 0xff000000;
		}
	}
}

static void
b2l_newline(void)
{
	b2l_col = 0;
	b2l_row = b2l_row + 1 < b2l_rows ? b2l_row + 1 : b2l_panic_floor;
	b2l_clear_row(b2l_row);
	b2l_clear_row(b2l_row + 1 < b2l_rows ? b2l_row + 1 : b2l_panic_floor);
}

void
b2l_early_putc(char c)
{
	unsigned owner = __atomic_load_n(&b2l_panic_owner, __ATOMIC_ACQUIRE);
	if (owner && owner != b2l_console_cpu()) return;
	b2l_log_append(c);
	if (!b2l_active || (__atomic_load_n(&b2l_gui,__ATOMIC_ACQUIRE) && !owner)) return;
	if (c == '\r') { b2l_col = 0; return; }
	if (c == '\n') { b2l_newline(); return; }
	if (b2l_col >= b2l_cols) b2l_newline();
	const unsigned char *glyph = iso_font + (unsigned char)c * 16;
	for (unsigned gy = 0; gy < 16; ++gy) {
		for (unsigned sy = 0; sy < b2l_scale; ++sy) {
			volatile uint32_t *line = b2l_fb +
			    (uint64_t)(b2l_row * 16 * b2l_scale + gy * b2l_scale + sy) * b2l_stride / 4 +
			    b2l_col * 8 * b2l_scale;
			for (unsigned gx = 0; gx < 8; ++gx) {
				uint32_t pixel = (glyph[gy] & (1U << gx)) ? 0xffffffff : 0xff000000;
				for (unsigned sx = 0; sx < b2l_scale; ++sx) line[gx * b2l_scale + sx] = pixel;
			}
		}
	}
	++b2l_col;
}

static void b2l_string(const char *s) { while (*s) b2l_early_putc(*s++); }

/* Bounded callers only: no allocation or regular printf/log locks. */
void
b2l_early_trace(const char *fmt, ...)
{
	if (!b2l_active) return;
	va_list args;
	va_start(args, fmt);
	_doprnt(fmt, &args, b2l_early_putc, 16);
	va_end(args);
	__asm__ volatile("dsb sy" ::: "memory");
}


void
b2l_early_init(const boot_args *args)
{
	uint64_t physical = args->Video.v_baseAddr & ~3ULL;
	uint64_t width = args->Video.v_width, height = args->Video.v_height;
	uint64_t stride = args->Video.v_rowBytes;
	if (!physical || width < 144 || height < 144 || width > 8192 || height > 8192 ||
	    stride < width * 4 || (stride & 3) || stride > 65536 ||
	    (args->Video.v_depth & 0xff) != 32 || stride * height > 32 * 1024 * 1024 ||
	    physical > UINT64_MAX - stride * height ||
	    (physical & 0x1fffff) + stride * height > 32 * 1024 * 1024 ||
	    physical < B2L_FB_RESERVED_BASE ||
	    physical + stride * height > B2L_FB_RESERVED_BASE + B2L_FB_RESERVED_SIZE ||
	    ((physical + stride * height + 0x1fffff) & ~0x1fffffULL) >
	    B2L_FB_RESERVED_BASE + B2L_FB_RESERVED_SIZE) return;
	b2l_fb = (volatile uint32_t *)(uintptr_t)physical;
	b2l_fb_physical = physical; b2l_fb_length = stride * height;
	b2l_width = (unsigned)width; b2l_height = (unsigned)height; b2l_stride = (unsigned)stride;
	b2l_scale = width >= 1200 ? 3 : (width >= 800 ? 2 : 1);
	b2l_cols = b2l_width / (8 * b2l_scale); b2l_rows = b2l_height / (16 * b2l_scale);
	/* Keep the first C line prominent in the upper half of the physical screen. */
	b2l_row = b2l_rows > 12 ? 10 : 0; b2l_col = 0; b2l_active = 1;
	b2l_clear_row(b2l_row);
	b2l_string("B2L XNU C ENTRY\n");
	b2l_early_trace("B2L RAM: arena=%llu MiB; bootstrap table pages=%u\n",
	    (uint64_t)args->memSize / (1024 * 1024), B2L_BOOTSTRAP_TABLE_PAGES);
}

void
b2l_early_mark(unsigned stage, const char *label)
{
	if (!b2l_active) return;
	b2l_string("B2L XNU ");
	if (stage >= 10) b2l_early_putc((char)('0' + stage / 10));
	b2l_early_putc((char)('0' + stage % 10)); b2l_string(": ");
	b2l_string(label); b2l_early_putc('\n');
	__asm__ volatile("dsb sy" ::: "memory");
}

static struct {
    unsigned claimed, ready, cpu;
    const char *message;
    uint64_t pc, lr, esr, far;
} b2l_first_fault;
void b2l_early_fault(const char *message, uint64_t pc, uint64_t lr, uint64_t esr, uint64_t far)
{
    unsigned expected = 0;
    if (!__atomic_compare_exchange_n(&b2l_first_fault.claimed, &expected, 1, 0,
            __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return;
    b2l_first_fault.cpu = b2l_console_cpu() - 1;
    b2l_first_fault.message = message;
    b2l_first_fault.pc = pc; b2l_first_fault.lr = lr;
    b2l_first_fault.esr = esr; b2l_first_fault.far = far;
    __atomic_store_n(&b2l_first_fault.ready, 1, __ATOMIC_RELEASE);
}

/* Capture typed values before panic handling starts; no foreign varargs. */
static struct {
    unsigned claimed, ready, name_space;
    int pid, signal, status;
    uint64_t code;
    char description[256];
} b2l_critical_exit;
void b2l_early_critical_exit(int pid,unsigned name_space,uint64_t code,int signal,int status,const char *description)
{
    unsigned expected=0;
    if(!__atomic_compare_exchange_n(&b2l_critical_exit.claimed,&expected,1,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return;
    b2l_critical_exit.pid=pid;b2l_critical_exit.name_space=name_space;b2l_critical_exit.code=code;
    b2l_critical_exit.signal=signal;b2l_critical_exit.status=status;
    const char *source=description?description:"none";
    unsigned n=0;while(n+1<sizeof(b2l_critical_exit.description)&&source[n]){b2l_critical_exit.description[n]=source[n];n++;}
    b2l_critical_exit.description[n]=0;
    __atomic_store_n(&b2l_critical_exit.ready,1,__ATOMIC_RELEASE);
}
void
b2l_early_panic(const char *fmt, va_list *args)
{
    (void)args;
    if (!b2l_active) return;
    unsigned expected = 0, owner = b2l_console_cpu();
    if (!__atomic_compare_exchange_n(&b2l_panic_owner, &expected, owner, 0,
            __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return;
    b2l_row = b2l_col = 0;
    for (unsigned row = 0; row < b2l_rows; ++row) b2l_clear_row(row);
    b2l_early_trace("B2L FIRST PANIC: MPIDR=0x%x\n", owner - 1);
    /* Record and render typed fault fields, without replaying foreign varargs.
     * Native panic handling still prints its complete message below the band. */
    if (__atomic_load_n(&b2l_first_fault.ready, __ATOMIC_ACQUIRE)) {
        b2l_string(b2l_first_fault.message); b2l_early_putc('\n');
        b2l_early_trace("B2L FAULT: MPIDR=0x%x pc=0x%llx lr=0x%llx\n esr=0x%llx far=0x%llx\n",
            b2l_first_fault.cpu, b2l_first_fault.pc, b2l_first_fault.lr,
            b2l_first_fault.esr, b2l_first_fault.far);
    } else if(__atomic_load_n(&b2l_critical_exit.ready,__ATOMIC_ACQUIRE)) {
        b2l_early_trace("B2L INIT EXIT: pid=%d ns=%u code=0x%llx signal=%d status=%d\n",
            b2l_critical_exit.pid,b2l_critical_exit.name_space,b2l_critical_exit.code,
            b2l_critical_exit.signal,b2l_critical_exit.status);
        b2l_string("B2L INIT REASON: ");b2l_string(b2l_critical_exit.description);b2l_early_putc('\n');
    } else {
        b2l_string("B2L PANIC FORMAT: ");
        for (unsigned i = 0; i < 160 && fmt[i] && fmt[i] != '\n'; ++i)
            b2l_early_putc(fmt[i]);
        b2l_early_putc('\n');
    }
    if(!__atomic_load_n(&b2l_critical_exit.ready,__ATOMIC_ACQUIRE))b2l_smp_dump();
    b2l_panic_floor = b2l_row + 2 < b2l_rows / 2 ? b2l_row + 2 : b2l_rows / 2;
    b2l_row = b2l_panic_floor; b2l_col = 0;
    __asm__ volatile("dsb sy" ::: "memory");
}

void b2l_early_relocate(uint64_t base)
{
	if (!b2l_active) return;
	b2l_fb = (volatile uint32_t *)(uintptr_t)base;
	b2l_early_mark(11, "kernel framebuffer alias live");
}
int b2l_early_range(uint64_t *physical, uint64_t *length)
{
	if (!b2l_active) return 0;
	*physical = b2l_fb_physical; *length = b2l_fb_length; return 1;
}
/* Used after VM bootstrap to attach controls to the existing mapping. */
uint64_t b2l_early_virtual(void)
{
	return b2l_active ? (uint64_t)(uintptr_t)b2l_fb : 0;
}

int b2l_early_available(void) { return b2l_active != 0; }
#endif
