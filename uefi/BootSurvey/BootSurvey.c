/** @file
  Read-only survey of the UEFI -> XNU boundary on beyond2lte.
  No disk writes, ExitBootServices call, or kernel jump.
**/
#include <Uefi.h>
#include <Library/ArmLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiApplicationEntryPoint.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Protocol/GraphicsOutput.h>

#define B2L_MIN_CANDIDATE_SIZE  (64ULL * 1024ULL * 1024ULL)

STATIC
CONST CHAR16 *
PixelFormatName (EFI_GRAPHICS_PIXEL_FORMAT Format)
{
  switch (Format) {
  case PixelRedGreenBlueReserved8BitPerColor: return L"RGBX8888";
  case PixelBlueGreenRedReserved8BitPerColor: return L"BGRX8888";
  case PixelBitMask: return L"bitmask";
  case PixelBltOnly: return L"BLT-only";
  default: return L"unknown";
  }
}

EFI_STATUS
EFIAPI
UefiMain (EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop = NULL;
  EFI_MEMORY_DESCRIPTOR *Map = NULL;
  EFI_MEMORY_DESCRIPTOR *Desc;
  EFI_STATUS Status;
  UINTN MapSize = 0;
  UINTN MapKey = 0;
  UINTN DescSize = 0;
  UINT32 DescVersion = 0;
  UINTN Offset;
  UINT64 BestBase = 0;
  UINT64 BestSize = 0;
  UINT64 Size;
  UINTN CandidateCount = 0;
  UINTN Attempt;
  UINT64 Mpidr, Mmfr0, CounterHz, Sctlr;

  __asm__ volatile ("mrs %0, mpidr_el1" : "=r" (Mpidr));
  __asm__ volatile ("mrs %0, id_aa64mmfr0_el1" : "=r" (Mmfr0));
  __asm__ volatile ("mrs %0, cntfrq_el0" : "=r" (CounterHz));
  __asm__ volatile ("mrs %0, sctlr_el1" : "=r" (Sctlr));

  Print (L"B2L XNU survey: CurrentEL=EL%u\n", (UINT32)(ArmReadCurrentEL () >> 2));
  Print (L"CPU: MPIDR=0x%lx MMFR0=0x%lx CNTFRQ=%lu SCTLR=0x%lx\n",
    Mpidr, Mmfr0, CounterHz, Sctlr);
  Status = gBS->LocateProtocol (&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **)&Gop);
  if (!EFI_ERROR (Status) && Gop != NULL && Gop->Mode != NULL && Gop->Mode->Info != NULL) {
    Print (L"GOP: %ux%u stride=%u format=%s framebuffer=0x%lx size=0x%lx\n",
      Gop->Mode->Info->HorizontalResolution,
      Gop->Mode->Info->VerticalResolution,
      Gop->Mode->Info->PixelsPerScanLine,
      PixelFormatName (Gop->Mode->Info->PixelFormat),
      Gop->Mode->FrameBufferBase,
      Gop->Mode->FrameBufferSize);
  } else {
    Print (L"GOP unavailable: %r\n", Status);
  }

  for (Attempt = 0; Attempt < 4; Attempt++) {
    if (Map != NULL) {
      FreePool (Map);
      Map = NULL;
    }
    MapSize = 0;
    Status = gBS->GetMemoryMap (&MapSize, NULL, &MapKey, &DescSize, &DescVersion);
    if (Status != EFI_BUFFER_TOO_SMALL || DescSize < sizeof (EFI_MEMORY_DESCRIPTOR)) {
      Print (L"GetMemoryMap sizing failed: %r\n", Status);
      return EFI_DEVICE_ERROR;
    }
    if (MapSize > MAX_UINTN - 2 * DescSize) {
      return EFI_BAD_BUFFER_SIZE;
    }
    MapSize += 2 * DescSize;
    Map = AllocatePool (MapSize);
    if (Map == NULL) {
      return EFI_OUT_OF_RESOURCES;
    }
    Status = gBS->GetMemoryMap (&MapSize, Map, &MapKey, &DescSize, &DescVersion);
    if (Status != EFI_BUFFER_TOO_SMALL) {
      break;
    }
  }
  if (EFI_ERROR (Status) || DescSize < sizeof (EFI_MEMORY_DESCRIPTOR)) {
    Print (L"GetMemoryMap failed: %r\n", Status);
    FreePool (Map);
    return Status;
  }

  Print (L"EFI map: version=%u descriptors=%u descriptor_size=%u\n",
    DescVersion, (UINT32)(MapSize / DescSize), (UINT32)DescSize);
  for (Offset = 0; Offset + DescSize <= MapSize; Offset += DescSize) {
    Desc = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)Map + Offset);
    if (Desc->Type != EfiConventionalMemory || Desc->NumberOfPages > MAX_UINT64 / EFI_PAGE_SIZE) {
      continue;
    }
    Size = Desc->NumberOfPages * EFI_PAGE_SIZE;
    if (Size < B2L_MIN_CANDIDATE_SIZE || Desc->PhysicalStart > MAX_UINT64 - Size) {
      continue;
    }
    CandidateCount++;
    Print (L"RAM candidate: base=0x%lx size=0x%lx end=0x%lx\n",
      Desc->PhysicalStart, Size, Desc->PhysicalStart + Size);
    if (Size > BestSize) {
      BestSize = Size;
      BestBase = Desc->PhysicalStart;
    }
  }
  if (CandidateCount == 0) {
    Print (L"No >=64 MiB conventional-memory candidate found.\n");
  } else {
    Print (L"Largest candidate: base=0x%lx size=0x%lx (observation only)\n",
      BestBase, BestSize);
  }
  FreePool (Map);
  Print (L"No ExitBootServices or disk write performed.\n");
  return EFI_SUCCESS;
}
