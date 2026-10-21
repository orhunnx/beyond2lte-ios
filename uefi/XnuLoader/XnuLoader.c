/* SPDX-License-Identifier: BSD-2-Clause
 * Single-CPU, non-SPTM XNU ARM64 handoff for the existing beyond2lte UEFI.
 * No partition writes, persistent variables, synthetic entropy, or PAC emulation.
 */
#include <Uefi.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/Rng.h>
#include <Guid/FileInfo.h>
#include <Library/ArmLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include "MachO.h"
#include "DeviceTree.h"
#include "BootPayload.h"
#include "Progress.h"
#include "Arena.h"
STATIC_ASSERT(B2L_SEED_BYTES == B2L_BOOT_SEED_SIZE, "BOOT payload and XNU seed sizes must match");

#define B2L_DT_SPACE (64 * 1024)
#define B2L_MAP_SPACE (1024 * 1024)
#define B2L_MAX_KERNEL (128 * 1024 * 1024)

VOID EFIAPI B2LXnuHandoff(UINT64 EntryPhysical, UINT64 BootArgsPhysical,
                          const struct b2l_handoff_info *Info);

static EFI_STATUS ReadFile(EFI_FILE_PROTOCOL *Root, CHAR16 *Path,
                           UINTN Maximum, VOID **Data, UINTN *Length)
{
    EFI_FILE_PROTOCOL *File = NULL;
    EFI_FILE_INFO *Info = NULL;
    EFI_STATUS Status;
    UINTN InfoSize = 0, ReadSize;
    VOID *Buffer = NULL;
    *Data = NULL; *Length = 0;
    Status = Root->Open(Root, &File, Path, EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(Status)) return Status;
    Status = File->GetInfo(File, &gEfiFileInfoGuid, &InfoSize, NULL);
    if (Status != EFI_BUFFER_TOO_SMALL || InfoSize < SIZE_OF_EFI_FILE_INFO || InfoSize > 65536) {
        if (!EFI_ERROR(Status)) Status = EFI_BAD_BUFFER_SIZE;
        goto Done;
    }
    Info = AllocatePool(InfoSize);
    if (Info == NULL) { Status = EFI_OUT_OF_RESOURCES; goto Done; }
    Status = File->GetInfo(File, &gEfiFileInfoGuid, &InfoSize, Info);
    if (EFI_ERROR(Status)) goto Done;
    if ((Info->Attribute & EFI_FILE_DIRECTORY) || Info->FileSize == 0 ||
        Info->FileSize > Maximum) { Status = EFI_BAD_BUFFER_SIZE; goto Done; }
    ReadSize = (UINTN)Info->FileSize;
    Buffer = AllocatePool(ReadSize);
    if (!Buffer) { Status = EFI_OUT_OF_RESOURCES; goto Done; }
    Status = File->Read(File, &ReadSize, Buffer);
    if (EFI_ERROR(Status)) goto Done;
    if (ReadSize != Info->FileSize) { Status = EFI_DEVICE_ERROR; goto Done; }
    *Data = Buffer; *Length = ReadSize; Buffer = NULL;
Done:
    if (Buffer) FreePool(Buffer);
    if (Info) FreePool(Info);
    File->Close(File);
    return Status;
}

static EFI_STATUS OpenRoot(EFI_HANDLE ImageHandle, EFI_FILE_PROTOCOL **Root)
{
    EFI_LOADED_IMAGE_PROTOCOL *Image;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Fs;
    EFI_STATUS Status;
    Status = gBS->HandleProtocol(ImageHandle, &gEfiLoadedImageProtocolGuid, (VOID **)&Image);
    if (EFI_ERROR(Status)) return Status;
    Status = gBS->HandleProtocol(Image->DeviceHandle, &gEfiSimpleFileSystemProtocolGuid, (VOID **)&Fs);
    if (EFI_ERROR(Status)) return Status;
    return Fs->OpenVolume(Fs, Root);
}

static EFI_STATUS GetSeed(EFI_FILE_PROTOCOL *Root, BOOLEAN HavePayloadSeed, UINT8 Seed[B2L_SEED_BYTES])
{
    EFI_RNG_PROTOCOL *Rng;
    EFI_STATUS Status;
    UINT8 RngSeed[B2L_SEED_BYTES];
    VOID *SeedFile;
    UINTN SeedSize;
    Status = gBS->LocateProtocol(&gEfiRngProtocolGuid, NULL, (VOID **)&Rng);
    if (!EFI_ERROR(Status)) {
        Status = Rng->GetRNG(Rng, NULL, sizeof(RngSeed), RngSeed);
        if (!EFI_ERROR(Status)) {
            CopyMem(Seed, RngSeed, sizeof(RngSeed)); ZeroMem(RngSeed, sizeof(RngSeed));
            return EFI_SUCCESS;
        }
        ZeroMem(RngSeed, sizeof(RngSeed));
    }
    if (HavePayloadSeed) return EFI_SUCCESS;
    if (!Root) return EFI_NOT_FOUND;
    Status = ReadFile(Root, L"\\EFI\\B2L\\seed.bin", B2L_SEED_BYTES, &SeedFile, &SeedSize);
    if (EFI_ERROR(Status)) return Status;
    if (SeedSize != B2L_SEED_BYTES) { ZeroMem(SeedFile, SeedSize); FreePool(SeedFile); return EFI_BAD_BUFFER_SIZE; }
    CopyMem(Seed, SeedFile, B2L_SEED_BYTES);
    ZeroMem(SeedFile, SeedSize); FreePool(SeedFile);
    return EFI_SUCCESS;
}

static UINT64 ReadCurrentEl(VOID)
{ UINT64 V; __asm__ volatile("mrs %0, CurrentEL" : "=r"(V)); return V >> 2; }
static UINT64 ReadMpidr(VOID)
{ UINT64 V; __asm__ volatile("mrs %0, mpidr_el1" : "=r"(V)); return V; }
static UINT32 ReadCounterFrequency(VOID)
{ UINT64 V; __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(V)); return (UINT32)V; }
static UINT64 ReadSctlr(VOID)
{ UINT64 V; __asm__ volatile("mrs %0, sctlr_el1" : "=r"(V)); return V; }

/* The MMU-off trampoline must execute at its physical address, including its
 * return path and stack. Firmware on this board uses a flat address map; reject
 * firmware that violates that prerequisite instead of jumping through it. */
static BOOLEAN IdentityAddress(UINT64 Address)
{
    UINT64 Par;
    if (!(ReadSctlr() & 1)) return TRUE;
    __asm__ volatile("at s1e1r, %1\n\tisb\n\tmrs %0, par_el1"
                     : "=r"(Par) : "r"(Address) : "memory");
    if (Par & 1) return FALSE;
    return (Par & 0x0000fffffffff000ULL) == (Address & 0x0000fffffffff000ULL);
}
static BOOLEAN IdentityRange(UINT64 Address, UINT64 Length)
{
    UINT64 End;
    if (Length == 0 || Address > MAX_UINT64 - (Length - 1)) return FALSE;
    End = Address + Length - 1;
    if (!IdentityAddress(Address) || !IdentityAddress(End)) return FALSE;
    for (Address = (Address & ~4095ULL); Address < End;) {
        if (!IdentityAddress(Address)) return FALSE;
        if (End - Address < 4096) break;
        Address += 4096;
    }
    return TRUE;
}

static EFI_STATUS GetVideo(struct b2l_boot_video *Video)
{
    EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop;
    EFI_STATUS Status = gBS->LocateProtocol(&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **)&Gop);
    UINT64 Required;
    if (EFI_ERROR(Status)) return Status;
    if (!Gop->Mode || !Gop->Mode->Info) return EFI_UNSUPPORTED;
    /* XNU's early console assumes little endian B,G,R,X storage. */
    if (Gop->Mode->Info->PixelFormat != PixelBlueGreenRedReserved8BitPerColor)
        return EFI_UNSUPPORTED;
    Video->base = Gop->Mode->FrameBufferBase;
    Video->display = 0;
    Video->width = Gop->Mode->Info->HorizontalResolution;
    Video->height = Gop->Mode->Info->VerticalResolution;
    Video->row_bytes = (UINT64)Gop->Mode->Info->PixelsPerScanLine * 4;
    Video->depth = 32;
    if (!Video->height || Video->row_bytes > MAX_UINT64 / Video->height)
        return EFI_BAD_BUFFER_SIZE;
    Required = Video->row_bytes * Video->height;
    if (Video->base > MAX_UINT64 - Required) return EFI_BAD_BUFFER_SIZE;
    if (!Video->base || (Video->base & 3) || Video->width < 1184 || Video->height < 864 ||
        Gop->Mode->Info->PixelsPerScanLine < Video->width ||
        Required > Gop->Mode->FrameBufferSize) return EFI_BAD_BUFFER_SIZE;
    return EFI_SUCCESS;
}

/* Select only EFI conventional memory; never widen the firmware memory map.
 * Exact-address allocation avoids an arbitrary fixed-size request and keeps
 * descriptor boundaries, framebuffer, firmware and DMA reservations intact. */
static EFI_STATUS AllocateArena(VOID *Map, EFI_PHYSICAL_ADDRESS *Base,
                                UINT64 *Size, UINTN *Pages)
{
    EFI_STATUS Status = EFI_OUT_OF_RESOURCES;
    UINTN MapSize, Key, DescSize;
    UINT32 Version;
    for (UINTN Retry=0; Retry<4; ++Retry) {
        UINT64 BestStart=0, BestSize=0;
        MapSize=B2L_MAP_SPACE;
        Status=gBS->GetMemoryMap(&MapSize, Map, &Key, &DescSize, &Version);
        if (EFI_ERROR(Status)) return Status;
        STATIC_ASSERT(OFFSET_OF(EFI_MEMORY_DESCRIPTOR,PhysicalStart)==8,"EFI descriptor ABI");
        STATIC_ASSERT(OFFSET_OF(EFI_MEMORY_DESCRIPTOR,NumberOfPages)==24,"EFI descriptor ABI");
        STATIC_ASSERT(EfiConventionalMemory==7,"EFI descriptor type ABI");
        int Choice=b2l_choose_arena(Map,MapSize,DescSize,&BestStart,&BestSize);
        if(Choice==-1) return EFI_COMPROMISED_DATA;
        if(Choice) return EFI_OUT_OF_RESOURCES;
        EFI_PHYSICAL_ADDRESS Candidate=BestStart;
        UINTN CandidatePages=EFI_SIZE_TO_PAGES(BestSize);
        Status=gBS->AllocatePages(AllocateAddress,EfiLoaderData,CandidatePages,&Candidate);
        if(!EFI_ERROR(Status)){*Base=Candidate; *Pages=CandidatePages; *Size=BestSize; return EFI_SUCCESS;}
    }
    return Status;
}

EFI_STATUS EFIAPI UefiMain(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    EFI_STATUS Status;
    EFI_FILE_PROTOCOL *Root = NULL;
    EFI_LOADED_IMAGE_PROTOCOL *Image;
    VOID *Kernel = NULL, *MemoryMap = NULL;
    UINTN KernelSize = 0, Pages = 0, DtSize, MapSize, MapKey, DescSize, Attempt;
    UINT32 DescVersion;
    EFI_PHYSICAL_ADDRESS Allocation = 0;
    UINT64 Base, ArenaSize=0, DtPhysical, ArgsPhysical, Used, Entry, Stack;
    struct b2l_handoff_info Handoff;
    BOOLEAN PayloadSeed = FALSE;
    struct b2l_macho Mach;
    struct b2l_dt_config Dt;
    struct b2l_boot_args *Args;
    int ParseResult;
    (VOID)SystemTable;
    ZeroMem(&Dt, sizeof(Dt));
    Print(L"beyond2lte XNU ARM64 loader (single CPU)\r\n");
    if (ReadCurrentEl() != 1) {
        Print(L"Unsupported CurrentEL: EL1 is required.\r\n");
        return EFI_UNSUPPORTED;
    }
    Status = B2lLoadBootPayload(&Kernel, &KernelSize, Dt.random_seed);
    if (!EFI_ERROR(Status)) {
        PayloadSeed = TRUE;
        if (!EFI_ERROR(B2lGetBootUfsLun(&Dt.boot_ufs_lun))) Dt.has_boot_ufs_lun = 1;
        else Print(L"B2L: BOOT UFS device path unrecognized; native storage will remain unavailable.\r\n");
    }
    else if (Status == EFI_NOT_FOUND || Status == EFI_UNSUPPORTED) {
        Status = OpenRoot(ImageHandle, &Root);
        if (!EFI_ERROR(Status))
            Status = ReadFile(Root, L"\\EFI\\B2L\\kernel", B2L_MAX_KERNEL, &Kernel, &KernelSize);
    }
    if (EFI_ERROR(Status)) { Print(L"Cannot read XNU payload: %r\r\n", Status); goto Fail; }
    ParseResult = b2l_parse_macho(Kernel, KernelSize, &Mach);
    if (ParseResult != 0) {
        Print(L"Refusing Mach-O: parser error %d\r\n", ParseResult);
        Status = EFI_LOAD_ERROR; goto Fail;
    }
    Used = Mach.virtual_end - Mach.virtual_base;
    if (Used > B2L_MIN_ARENA - B2L_DT_SPACE - sizeof(*Args) - 8192) {
        Status = EFI_BAD_BUFFER_SIZE; goto Fail;
    }
    Status = GetSeed(Root, PayloadSeed, Dt.random_seed);
    if (EFI_ERROR(Status)) {
        Print(L"No genuine entropy: EFI RNG or a fresh 256-byte host seed is required.\r\n");
        goto Fail;
    }
    Status = GetVideo(&Dt.video);
    if (EFI_ERROR(Status)) {
        Print(L"Unsupported GOP framebuffer: %r\r\n", Status); goto Fail;
    }
    Dt.counter_frequency = ReadCounterFrequency(); Dt.mpidr = ReadMpidr();
    if (!Dt.counter_frequency) { Status = EFI_UNSUPPORTED; goto Fail; }
    MemoryMap = AllocatePool(B2L_MAP_SPACE);
    if (!MemoryMap) { Status=EFI_OUT_OF_RESOURCES; goto Fail; }
    Status = AllocateArena(MemoryMap, &Allocation, &ArenaSize, &Pages);
    if (EFI_ERROR(Status)) { Print(L"No contiguous EFI conventional-memory arena of at least 1 GiB: %r\r\n",Status); goto Fail; }
    Base = Allocation;
    /* Only this reserved EFI allocation is advertised as normal RAM to XNU.
     * Firmware, framebuffer and UFS/MMIO ranges are never added as free RAM. */
    Dt.memory_base = Base; Dt.memory_size = ArenaSize;
    for (UINTN I = 0; I < Mach.segment_count; ++I) {
        struct b2l_segment *S = &Mach.segments[I];
        VOID *Dest = (VOID *)(UINTN)(Base + S->va - Mach.virtual_base);
        ZeroMem(Dest, (UINTN)S->size);
        CopyMem(Dest, (UINT8 *)Kernel + S->file_offset, (UINTN)S->file_size);
    }
    DtPhysical = ALIGN_VALUE(Base + Used, 4096);
    Status = b2l_build_device_tree((VOID *)(UINTN)DtPhysical, B2L_DT_SPACE, &Dt, &DtSize) == 0
             ? EFI_SUCCESS : EFI_BAD_BUFFER_SIZE;
    ZeroMem(Dt.random_seed, sizeof(Dt.random_seed));
    if (EFI_ERROR(Status)) goto Fail;
    ArgsPhysical = ALIGN_VALUE(DtPhysical + DtSize, 4096);
    Args = (VOID *)(UINTN)ArgsPhysical;
    ZeroMem(Args, sizeof(*Args));
    Args->revision = 2; Args->version = 2;
    Args->virtual_base = Mach.virtual_base; Args->physical_base = Base;
    Args->memory_size = ArenaSize; Args->actual_memory_size = ArenaSize;
    Args->top_of_kernel_data = ALIGN_VALUE(ArgsPhysical + sizeof(*Args), 4096);
    Args->device_tree = Mach.virtual_base + DtPhysical - Base;
    Args->device_tree_length = (UINT32)DtSize;
    CopyMem(&Args->video, &Dt.video, sizeof(Dt.video));
    /* Bit zero requests a physical framebuffer mapping even after VM init. */
    Args->video.base |= 1;
    AsciiStrCpyS(Args->command_line, sizeof(Args->command_line),
                "-v debug=0x14e serial=3 cpus=1 keepsyms=1 -enable_kprintf_spam");
    Entry = Base + Mach.entry - Mach.virtual_base;
    FreePool(Kernel); Kernel = NULL;
    if (Root) { Root->Close(Root); Root = NULL; }
    Status = gBS->HandleProtocol(ImageHandle, &gEfiLoadedImageProtocolGuid, (VOID **)&Image);
    if (EFI_ERROR(Status)) goto Fail;
    __asm__ volatile("mov %0, sp" : "=r"(Stack));
    if (!IdentityRange((UINTN)Image->ImageBase, Image->ImageSize) ||
        !IdentityRange(Stack & ~4095ULL, 4096) ||
        !IdentityRange(Base, Args->top_of_kernel_data - Base)) {
        Print(L"Refusing non-identity firmware mapping before MMU-off handoff.\r\n");
        Status = EFI_UNSUPPORTED; goto Fail;
    }
    Status = gBS->SetWatchdogTimer(0, 0, 0, NULL);
    if (EFI_ERROR(Status)) goto Fail;
    Print(L"XNU entry PA 0x%lx, boot_args PA 0x%lx, RAM 0x%lx + %lu MiB\r\n", Entry, ArgsPhysical, Base, ArenaSize/(1024*1024));
    Print(L"GOP %lux%lu, CNTFRQ %u Hz; exiting EFI Boot Services.\r\n",
          Dt.video.width, Dt.video.height, Dt.counter_frequency);
    ZeroMem(&Handoff,sizeof(Handoff));
    Handoff.kernel_start=Base; Handoff.kernel_end=Args->top_of_kernel_data;
    Handoff.image_start=(UINTN)Image->ImageBase; Handoff.image_end=Handoff.image_start+Image->ImageSize;
    Handoff.stack_start=Stack-16384; Handoff.stack_end=Stack+4096;
    if(!IdentityRange(Handoff.stack_start,Handoff.stack_end-Handoff.stack_start)) {
        Status=EFI_UNSUPPORTED; goto Fail;
    }
    B2lProgressPrepare(Dt.video.base,Dt.video.row_bytes,Dt.video.width,Dt.video.height,&Handoff);
    /* No console, allocation, protocol or other boot-service calls after the
     * first ExitBootServices attempt. Firmware may invalidate the map key. */
    for (Attempt = 0; Attempt < 8; ++Attempt) {
        MapSize = B2L_MAP_SPACE;
        Status = gBS->GetMemoryMap(&MapSize, MemoryMap, &MapKey, &DescSize, &DescVersion);
        if (EFI_ERROR(Status)) { B2lProgressSet(&Handoff,0,0x00ff0000); CpuDeadLoop(); }
        B2lProgressSet(&Handoff,0,0x0000ff00);
        B2lProgressSet(&Handoff,1,0x00ffff00);
        Status = gBS->ExitBootServices(ImageHandle, MapKey);
        if (!EFI_ERROR(Status)) {
            B2lProgressSet(&Handoff,1,0x0000ff00);
            B2LXnuHandoff(Entry, ArgsPhysical, &Handoff);
            CpuDeadLoop();
        }
        B2lProgressSet(&Handoff,1,0x00ff0000);
        if (Status != EFI_INVALID_PARAMETER) CpuDeadLoop();
    }
    CpuDeadLoop();
    return EFI_ABORTED;
Fail:
    ZeroMem(Dt.random_seed, sizeof(Dt.random_seed));
    if (Root) Root->Close(Root);
    if (Kernel) FreePool(Kernel);
    if (MemoryMap) FreePool(MemoryMap);
    if (Allocation) gBS->FreePages(Allocation, Pages);
    return Status;
}
