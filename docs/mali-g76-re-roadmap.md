# Exynos 9820 (`beyond2lte`) Mali-G76 MP12 (`G3D`) Reverse-Engineering & Bring-Up Reference

## 1. Current Graphics Pipeline & Why `IOAcceleratorES` Must Not Be Stubbed Early

On iOS 26.6.2 (`23G90`), `backboardd` / `RenderServer` (`QuartzCore`) selects its rendering backend in `CA::Render::Update::added_context2`:

1. When an `IOAcceleratorES` IOKit service is present in the registry, `AccelServer::render_update` attempts to create a hardware `CAMetalContext` (`CAMetalContextCreate`) through the userspace `MetalPlugin` bundle matched to the accelerator's IOKit properties.
2. Only when `IOAcceleratorES` is **absent** does `QuartzCore` fall back to `Server::sw_renderer` (`SWContext`), which renders layers on the CPU into `IOSurface` buffers (`B2LSurface`) and submits them via `AppleCLCD` (`B2LFramebuffer`) IOMFB `swap_submit`.

As documented in `docs/build-0061.md:12-16`, publishing an incomplete or stub `IOAcceleratorES` service before a complete userspace `MetalPlugin` + AIR-to-Bifrost shader compiler + Mali-G76 MMU/Job Manager backend exists causes `QuartzCore` to abandon `SWContext` (`Server::sw_renderer`), breaking screen output entirely.

### CPU Scanout Fast Path (`B2LBlit.h` & `B2LFramebuffer.cpp`)

While `SWContext` drives scanout on `beyond2lte`:
- `xnu-overlay/iokit/Kernel/arm/B2LBlit.h` uses a single contiguous `memmove((void *)dst, (const void *)src, (size_t)h * ds)` whenever `sx == 0 && dx == 0 && (uint64_t)w * 4 == ds && ds == ss` (full-width `1440x3040` `BGRA`/`ARGB` surfaces, `17,510,400` bytes per frame), avoiding `3,040` per-row calls.
- `xnu-overlay/iokit/Kernel/arm/B2LFramebuffer.cpp` maintains the `0093` atomic swap telemetry counters (`b2l_fb_received`, `b2l_fb_copied`, `b2l_fb_disabled`, `b2l_fb_missing`, `b2l_fb_unsupported`, `b2l_fb_bounds`, `b2l_fb_last_swap`, `b2l_fb_last_mask`, `b2l_fb_last_disabled`, `b2l_fb_last_id`, `b2l_fb_last_format`) and logs `B2L FB PROGRESS` every `600` vsync ticks (`~10` seconds at `60 Hz`).

---

## 2. Exynos 9820 Mali-G76 MP12 (`mali@18500000`) Hardware Map

Extracted from the Samsung Exynos 9820 (`beyond2lte`) kernel device tree (`arch/arm64/boot/dts/exynos/exynos9820.dts` @ `61b11dc765d44e6619126d0908b70d5f26368111`) and `drivers/gpu/arm/b_r19p0` / `drivers/gpu/arm/exynos`:

### 2.1 MMIO, Interrupts, and Power Domain

| Resource | Value | Notes |
| :--- | :--- | :--- |
| Device Tree Node | `mali@18500000` (`compatible = "arm,mali"`) | Bifrost `tNOx` (`GPU_ID2_PRODUCT_TNOX = GPU_ID2_MODEL_MAKE(7, 1)`) |
| MMIO Base & Size | `0x18500000`, size `0x5000` (`20 KiB`) | Uncached device memory (`VM_WIMG_IO`) |
| `JOB` Interrupt | GICv2 SPI `0x120` (`288`) -> Hardware INTID `320` | `interrupt-names = "JOB", "MMU", "GPU"` |
| `MMU` Interrupt | GICv2 SPI `0x11f` (`287`) -> Hardware INTID `319` | Stage-1 page fault / bus fault handler |
| `GPU` Interrupt | GICv2 SPI `0x121` (`289`) -> Hardware INTID `321` | Power state transitions, reset, GPU faults |
| PMU System Controller | `0x15860000` (`/system-controller@15860000`) | Exynos Alive/PMU register bank |
| `G3D_STATUS` Offset | `0x2104` -> Physical `0x15862104` | `gpu_pmu_status_reg_offset = <0x2104>` |
| `G3D_STATUS` Power Mask | `0x1` (`LOCAL_PWR_CFG`) | `gpu_pmu_status_local_pwr_mask = <0x1>` |
| CAL Clock / Power IDs | `g3d_cmu_cal_id = <0xb04000a>` | `samsung,power-domain = <0x32>` (`pd-embedded_g3d`) |

> [!CAUTION]
> Reading any register inside `0x18500000..0x18504FFF` while `(*(volatile uint32_t *)0x15862104 & 0x1) == 0` triggers an immediate synchronous external abort (`SError` / bus hang) because the `G3D` power domain and AXI clock bridge are gated off by default after UEFI exit.

### 2.2 Bifrost (`tNOx`) Control Register Offsets (`0x18500000` + offset)

From `drivers/gpu/arm/b_r19p0/mali_midg_regmap.h`:

| Offset | Name | Access | Description |
| :--- | :--- | :--- | :--- |
| `0x0000` | `GPU_ID` | `RO` | Architecture/product/revision (`GPU_ID2_PRODUCT_TNOX`: `arch_major=7`, `product_major=1`) |
| `0x0004` | `L2_FEATURES` | `RO` | Level 2 cache slice size and associativity |
| `0x000C` | `TILER_FEATURES` | `RO` | Hierarchical tiler bin size and active mask |
| `0x0010` | `MEM_FEATURES` | `RO` | Coherent core group / L2 slice count |
| `0x0014` | `MMU_FEATURES` | `RO` | VA/PA bit width (`48-bit` VA, `40-bit` PA) |
| `0x0018` | `AS_PRESENT` | `RO` | Bitmask of implemented MMU address spaces (`AS0..AS15`) |
| `0x0020` | `GPU_IRQ_RAWSTAT` | `RW` | Raw GPU interrupt status |
| `0x0024` | `GPU_IRQ_CLEAR` | `WO` | Clear latched GPU interrupts |
| `0x0028` | `GPU_IRQ_MASK` | `RW` | Enabled GPU interrupt mask |
| `0x002C` | `GPU_IRQ_STATUS` | `RO` | Masked active GPU interrupt status |
| `0x0030` | `GPU_COMMAND` | `WO` | Soft reset (`0x01`), hard reset (`0x02`), cache clean/inv |
| `0x0034` | `GPU_STATUS` | `RO` | Reset active, power active, protected mode status |
| `0x003C` | `GPU_FAULTSTATUS` | `RO` | Exception type and fault source ID |
| `0x0040` | `GPU_FAULTADDRESS_LO` | `RO` | Faulting virtual address (`[31:0]`) |
| `0x0044` | `GPU_FAULTADDRESS_HI` | `RO` | Faulting virtual address (`[63:32]`) |
| `0x0100` | `SHADER_PRESENT_LO` | `RO` | Present shader core bitmap (`MP12` -> `12` cores across `3` stacks) |
| `0x0110` | `TILER_PRESENT_LO` | `RO` | Present tiler bitmap |
| `0x0120` | `L2_PRESENT_LO` | `RO` | Present L2 cache bitmap |
| `0x0140` | `SHADER_READY_LO` | `RO` | Powered and clocked shader core bitmap |
| `0x0150` | `TILER_READY_LO` | `RO` | Powered and clocked tiler bitmap |
| `0x0160` | `L2_READY_LO` | `RO` | Powered and clocked L2 bitmap |
| `0x0180` | `SHADER_PWRON_LO` | `WO` | Request shader core power-on bitmap |
| `0x0190` | `TILER_PWRON_LO` | `WO` | Request tiler power-on bitmap |
| `0x01A0` | `L2_PWRON_LO` | `WO` | Request L2 cache power-on bitmap |
| `0x0E00` | `STACK_PRESENT_LO` | `RO` | Present core stack bitmap |
| `0x0E10` | `STACK_READY_LO` | `RO` | Ready core stack bitmap |
| `0x0E20` | `STACK_PWRON_LO` | `WO` | Request core stack power-on bitmap |
| `0x1000` | `JOB_CONTROL_BASE` | `RW` | Job Manager slots `JS0` (fragment), `JS1` (vertex/tiler/compute), `JS2` (compute) |
| `0x2000` | `MMU_BASE` | `RW` | MMU control and per-AS (`AS0..AS15`) `TRANSTAB`/`MEMATTR`/`TRANSCFG` banks |

### 2.3 Operating Points (DVFS Table from `exynos9820.dts`)

- `gpu_max_clock`: `702,000 kHz` (`0xab630`)
- `gpu_min_clock`: `156,000 kHz` (`0x26160`)
- `gpu_dvfs_start_clock`: `260,000 kHz` (`0x3f7a0`)
- `gpu_default_voltage`: `800,000 uV` (`0xc3500`)
- Operating clocks (`kHz`): `702000`, `650000`, `600000`, `546000`, `433000`, `377000`, `325000`, `260000`, `200000`, `156000`.

---

## 3. Staged Hardware Bring-Up Plan (Local PC + Connected S10+ `agy` CLI)

When the S10+ (`SM-G975F`) is connected to a local PC running `agy` CLI for iterative `boot.img` testing (which only requires flashing the `25.33 MiB` `BOOT` partition and **never** touches the `32 GiB` `USERDATA` rootfs):

### Phase 1: Safe Read-Only G3D Power & `GPU_ID` Probe (`b2l_gpu_probe=1`)
1. Gate all probe logic behind a kernel boot-arg (`PE_parse_boot_argn("b2l_gpu_probe", ...)`), default `0`.
2. Map `0x15862000` (`4 KiB` PMU page containing `0x15862104`) with `ml_io_map(0x15862000, 0x1000)`.
3. Read `G3D_STATUS` at offset `0x104` (`0x15862104`). Log `B2L GPU: PMU G3D_STATUS=0x%08x` to the persistent `SYSTEM` console log (`b2l_console`).
4. Only if `(g3d_status & 0x1) != 0`, map `0x18500000` (`0x5000`) and read `GPU_ID` (`+0x000`), `L2_FEATURES` (`+0x004`), `MMU_FEATURES` (`+0x014`), `AS_PRESENT` (`+0x018`), `SHADER_PRESENT_LO` (`+0x100`), `TILER_PRESENT_LO` (`+0x110`), and `L2_PRESENT_LO` (`+0x120`).
5. If `(g3d_status & 0x1) == 0`, enable the `pd-embedded_g3d` domain in `Mu-Silicium` UEFI before `ExitBootServices` (or via Exynos PMU `G3D_CONFIGURATION` at `0x15862100 = 0x1` followed by polling `0x15862104 & 0x1`) so `G3D` stays powered at `260 MHz` (`gpu_dvfs_start_clock`) across XNU handoff.

### Phase 2: Mali-G76 Soft Reset, L2/Shader Power-Up, and Stage-1 MMU (`B2LGPU.cpp`)
1. Issue `GPU_COMMAND = 0x01` (`SOFT_RESET`) and poll `GPU_IRQ_RAWSTAT` bit `8` (`RESET_COMPLETED`).
2. Power on `L2_PRESENT_LO`, `TILER_PRESENT_LO`, and `SHADER_PRESENT_LO` via `L2_PWRON_LO` (`+0x1A0`), `TILER_PWRON_LO` (`+0x190`), and `SHADER_PWRON_LO` (`+0x180`); poll `L2_READY_LO` / `TILER_READY_LO` / `SHADER_READY_LO`.
3. Configure `AS0` (`MMU_BASE + 0x400`) in `AARCH64_4K` (`LPAE`) mode pointing at a pinned XNU physical page table mapping a test `B2LSurface` (`IOSurface`) buffer, and verify a minimal `WRITE_VALUE` job descriptor on `JS1` (`JOB_CONTROL_BASE + 0x800 + 0x80`) completes and raises GICv2 INTID `320` (`JOB` IRQ).

### Phase 3: Display Co-Processor (`DECON` / `DPU` DMA) & Userspace `IOAcceleratorES`
1. Before exposing `IOAcceleratorES`, offload the `1440x3040` (`17.51 MiB`) framebuffer copy from the CPU (`B2LBlit.h`) to Exynos 9820 `IDMA`/`DPP` or a Mali-G76 `JS1` blit job inside `AppleCLCD::swap`. Because this operates beneath `AppleCLCD::swap`, it requires zero changes to `USERDATA` (`B2LRoot`) and keeps `SWContext` 100% intact.
2. Only after `23G90` `IOAcceleratorES` + `MetalPlugin` user-client selectors are bridged to `B2LGPUUserClient`, expose `IOAcceleratorES` behind a boot-arg (`b2l_gpu_accel=1`) so rollback to `SWContext` is always possible by flashing a `boot.img` without touching `userdata.img.gz.part1..5`.
