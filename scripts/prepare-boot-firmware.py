#!/usr/bin/env python3
"""Add the built XNU loader to a disposable external Mu firmware work tree.

The reference UEFI remains an external dependency. No ACPI/Windows sources are
copied into this repository. This script does not access a phone or flash disk.
"""
import argparse
import json
from pathlib import Path
import re
import shutil
import uuid


def replace_function(source, name, body):
    start = source.index(name + " (")
    brace = source.index("{", start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[:brace] + "{\n" + body + "\n}" + source[end:]


def xnu_memory_profile(source):
    """Remove broad budget guards; retain exact observed fixed reservations."""
    marker="// B2L discontiguous RAM profile v1"
    if marker in source:return source
    expected=('"Lower dynamic guard"','"Upper bank guard"','"B2L EL2 early memory"')
    if not any(x in source for x in expected):raise ValueError("unrecognized firmware memory profile")
    lines=[l for l in source.splitlines(True) if not any(x in l for x in expected)]
    source=''.join(lines)
    evidence=Path(__file__).resolve().parents[1]/"common/memory/beyond2lte-8g-dt.json"
    nodes=json.loads(evidence.read_text());entries=[]
    # Exact fixed reservations, except the existing separately named U-H row.
    for name,p in nodes.items():
        if not name.startswith('/reserved-memory/') or 'reg' not in p:continue
        raw=bytes.fromhex(p['reg'])
        if len(raw)!=12:raise ValueError("unexpected fixed reservation geometry")
        base=int.from_bytes(raw[:8],'big');size=int.from_bytes(raw[8:],'big')
        if base==0x87000000 and size==0x200000:continue
        entries.append('  {"B2L DT '+name.split('/')[-1]+'", 0x%XULL, 0x%XULL, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_THROUGH},\n'%(base,size))
    # These two low-bank gaps are absent from the live memory nodes.
    for base,size in ((0xBAB00000,0x5500000),(0xE0000000,0x1900000)):
        entries.append('  {"B2L absent DDR", 0x%XULL, 0x%XULL, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_THROUGH},\n'%(base,size))
    at=source.index('  // DDR Regions')
    source=source[:at]+marker+'\n'+''.join(entries)+source[at:]
    return source


def xnu_ram_manager_profile(source):
    """Normalize live secure-world holes and stop on unknown enabled features."""
    marker='// B2L RAM hole normalization v1'
    if marker in source:return source
    source=source.replace('STATIC EFI_PHYSICAL_ADDRESS MemoryStart;', 'STATIC EFI_STATUS B2lRamStatus=EFI_NOT_READY;\nSTATIC EFI_PHYSICAL_ADDRESS MemoryStart;',1)
    body="""  // B2L RAM hole normalization v1
  if(EFI_ERROR(B2lRamStatus))return B2lRamStatus;
  EFI_MEMORY_RANGE *Result=AllocateZeroPool(sizeof(EFI_MEMORY_RANGE)*6);
  if(Result==NULL)return EFI_OUT_OF_RESOURCES;
  EFI_MEMORY_RANGE Sorted[5];CopyMem(Sorted,HoleRange,sizeof(Sorted));
  for(UINTN i=0;i<5;i++){
    if(Sorted[i].Length && (Sorted[i].Address>MAX_UINT64-Sorted[i].Length)){
      FreePool(Result);return EFI_COMPROMISED_DATA;
    }
    for(UINTN j=i;j>0 && Sorted[j].Address<Sorted[j-1].Address;j--){
      EFI_MEMORY_RANGE T=Sorted[j];Sorted[j]=Sorted[j-1];Sorted[j-1]=T;
    }
  }
  UINT64 Cursor=MemoryStart;UINT8 Count=0;
  for(UINTN i=0;i<5;i++){
    UINT64 Base=Sorted[i].Address,End=Base+Sorted[i].Length;
    if(!Sorted[i].Length||End<=Cursor||Base>=MemoryEnd)continue;
    if(Base>Cursor){Result[Count].Address=Cursor;Result[Count++].Length=Base-Cursor;}
    if(End>MemoryEnd)End=MemoryEnd;
    if(End>Cursor)Cursor=End;
  }
  if(Cursor<MemoryEnd){Result[Count].Address=Cursor;Result[Count++].Length=MemoryEnd-Cursor;}
  if(!Count){FreePool(Result);return EFI_NOT_FOUND;}
  *Range=Result;*RangeCount=Count;return EFI_SUCCESS;"""
    source=replace_function(source,'GetUsableMemoryRanges',body)
    # Only optional PCD-disabled features return EFI_NOT_FOUND. Other failures
    # cannot be interpreted as permission to hand secure memory to an OS.
    begin=source.index('GetMemoryHoles ()');end=source.index('EFI_STATUS\nEFIAPI\nGetMemoryData',begin)
    part=source[begin:end]
    for i in range(4):
        needle='    HoleRange[%d].Length  = HoleLength;\n  }'%i
        if needle not in part:raise ValueError('external live-hole query changed')
        part=part.replace(needle,needle+' else if (Status != EFI_NOT_FOUND) {\n    return Status;\n  }',1)
    source=source[:begin]+part+source[end:]
    pos=source.index('GetMemoryData (');tail=source[pos:];brace=tail.index('{');tail=tail[:brace+1]+'\n  B2lRamStatus=EFI_NOT_READY;'+tail[brace+1:];at=tail.rfind('  return EFI_SUCCESS;');assert at>=0
    tail=tail[:at]+'  B2lRamStatus=EFI_SUCCESS;\n'+tail[at:];source=source[:pos]+tail
    return source


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--mu", type=Path, required=True, help="Disposable Mu-Silicium build tree")
    p.add_argument("--loader", type=Path, required=True)
    p.add_argument("--loader-guid", required=True)
    p.add_argument("--enable-touch", action="store_true", help="Initialize existing beyond2lte sec_ts driver before XNU handoff")
    a = p.parse_args()
    root = a.mu.resolve()
    guid = uuid.UUID(a.loader_guid)
    # Protect the user's named reference checkout from accidental build edits.
    if root.name == "UEFI_beyond2lte" or not (root / "Mu_Basecore").is_dir():
        p.error("--mu must be a disposable Mu-Silicium build tree, not the reference repository")
    platform = root / "Platforms/Samsung/beyond2ltePkg"
    if a.enable_touch:
        dxe = platform / "Include/DXE.inc"
        text = dxe.read_text()
        marker = "  # INF beyond2ltePkg/Drivers/B2LTouchDxe/B2LTouchDxe.inf"
        enabled = "  INF beyond2ltePkg/Drivers/B2LTouchDxe/B2LTouchDxe.inf"
        if marker not in text and enabled not in text:
            p.error("expected beyond2lte touch entry is absent")
        dxe.write_text(text.replace(marker, enabled))
        # The touch branch's yap.sh --touch enables BOTH entries. Enabling only
        # the FV module leaves HsI2cDxe linked to the null bus-data provider,
        # so its protocol (and hence TouchDxe's DEPEX) never becomes available.
        soc = root / "Silicon/Samsung/S5E9820Pkg/S5E9820Pkg.dsc.inc"
        binding = "  HsI2cLib|S5E9820Pkg/Library/HsI2cLib/HsI2cLib.inf"
        disabled = binding.replace("  HsI2cLib", "  # HsI2cLib", 1)
        soc_text = soc.read_text()
        if soc_text.count(disabled) == 1 and binding not in soc_text:
            soc_text = soc_text.replace(disabled, binding)
        if soc_text.splitlines().count(binding) != 1:
            p.error("expected exactly one enabled S5E9820 HsI2cLib binding")
        soc.write_text(soc_text)


    dsc_path = platform / "beyond2lte.dsc"
    fdf_path = platform / "beyond2lte.fdf"
    library_root = root / "Silicon/Silicium/SiliciumPkg"
    bindings_path = library_root / "SiliciumPkg.dsc.inc"
    manager_path = library_root / "Library/BootManagerLib/BootManager.c"
    manager_inf_path = library_root / "Library/BootManagerLib/BootManagerLib.inf"
    gic_path = root / "Mu_Basecore/ArmPkg/Drivers/ArmGicDxe/GicV2/ArmGicV2Dxe.c"
    memory_path = platform / "Library/MemoryMapLib/MemoryMapLib.c"
    ram_manager_path=root/"Silicon/Samsung/SamsungPkg/Library/RamManagerLib/RamManager.c"
    for path in (ram_manager_path,memory_path, dsc_path, fdf_path, bindings_path, manager_path, manager_inf_path, gic_path, a.loader):
        if not path.is_file():
            p.error(f"missing input: {path}")
    if re.search(r"MemoryMapLib\s*\|\s*beyond2ltePkg/Library/MemoryMapLib/MemoryMapLib\.inf", dsc_path.read_text()) is None:
        p.error("external MemoryMapLib binding changed")
    try:
        memory = xnu_memory_profile(memory_path.read_text())
        ram_manager = xnu_ram_manager_profile(ram_manager_path.read_text())
    except ValueError as error:
        p.error(str(error))
    bindings = bindings_path.read_text()
    if re.search(r"PlatformBootManagerLib\s*\|\s*SiliciumPkg/Library/BootManagerLib/BootManagerLib\.inf", bindings) is None:
        p.error("external firmware PlatformBootManagerLib binding changed; refusing an ineffective boot policy patch")
    manager = manager_path.read_text()
    before = "  MsBootOptionsLibRegisterDefaultBootOptions ();"
    before_disabled = "  // B2L XNU: do not register or rewrite persistent boot options."
    if before not in manager and before_disabled not in manager:
        p.error("expected PlatformBootManagerBeforeConsole registration call is missing")
    manager = manager.replace(before, before_disabled)
    for include in ("<PiDxe.h>", "<Library/BaseLib.h>", "<Library/DxeServicesLib.h>", "<Library/UefiLib.h>"):
        if f"#include {include}" not in manager:
            manager = f"#include {include}\n" + manager
    fields = guid.fields
    guid_initializer = "{ 0x%08X, 0x%04X, 0x%04X, { %s } }" % (
        fields[0], fields[1], fields[2], ", ".join(f"0x{b:02X}" for b in guid.bytes[8:]))
    try:
        manager = replace_function(manager, "PlatformBootManagerAfterConsole", f"""  // B2L XNU: execute before BDS consumes cached BootNext or existing BootOrder.
  // No boot option variables are created, removed or reordered by this path.
  EFI_STATUS Status;
  EFI_GRAPHICS_OUTPUT_PROTOCOL *GopProtocol = NULL;
  EFI_GUID LoaderGuid = {guid_initializer};
  EFI_HANDLE LoaderImage = NULL;
  VOID *LoaderBuffer = NULL;
  UINTN LoaderSize = 0;

  PostGopSetup (&GopProtocol);
  EfiBootManagerConnectAll ();
  Print (L"B2L: launching embedded XNU loader before BootNext/BootOrder\\r\\n");
  Status = GetSectionFromAnyFv (&LoaderGuid, EFI_SECTION_PE32, 0, &LoaderBuffer, &LoaderSize);
  if (!EFI_ERROR (Status)) {{
    Status = gBS->LoadImage (FALSE, gImageHandle, NULL, LoaderBuffer, LoaderSize, &LoaderImage);
    FreePool (LoaderBuffer);
    if (!EFI_ERROR (Status)) {{
      Status = gBS->StartImage (LoaderImage, NULL, NULL);
    }}
  }}
  // Returning would expose old Windows boot entries. Keep the failure visible.
  Print (L"B2L: XNU loader returned %r; automatic fallback stopped\\r\\n", Status);
  CpuDeadLoop ();""")
    except (ValueError, IndexError):
        p.error("expected PlatformBootManagerAfterConsole implementation is missing or malformed")
    manager_inf = manager_inf_path.read_text()
    for library in ("BaseLib", "DxeServicesLib", "UefiLib", "UefiBootManagerLib"):
        if re.search(rf"^\s*{library}\s*$", manager_inf, re.MULTILINE) is None:
            manager_inf = manager_inf.replace("[LibraryClasses]", f"[LibraryClasses]\n  {library}", 1)
    if "[LibraryClasses]" not in manager_inf:
        p.error("external BootManagerLib INF has no library class section")
    try:
        gic = replace_function(gic_path.read_text(), "ArmGicV2DisableInterruptInterface", """  // B2L XNU profile: preserve unrelated CPU-interface control bits.
  MmioAnd32 (GicInterruptInterfaceBase + ARM_GIC_ICCICR, ~1U);
  MmioWrite32 (GicInterruptInterfaceBase + ARM_GIC_ICCPMR, 0);""")
        gic = replace_function(gic, "GicV2ExitBootServicesEvent", """  // B2L XNU profile: bound shutdown even if a source cannot be quiesced.
  // CoreExitBootServices already stopped the firmware timer. Mask both
  // non-secure architected timer PPIs before draining the CPU interface.
  UINTN Index;
  UINT32 RawIar;
  UINT32 IntId;
  UINT32 CpuControl;
  (VOID)Event;
  (VOID)Context;

  CpuControl = MmioRead32 (mGicInterruptInterfaceBase + ARM_GIC_ICCICR);
  // B2L maps only the first 4 KiB CPU-interface window. GICC_DIR at +0x1000
  // is outside that mapping. Select combined non-secure EOI before draining,
  // matching the board's XNU GIC setup, and preserve every unrelated bit.
  CpuControl &= ~(1U << 9); // Clear EOImodeNS in the non-secure GICC_CTLR view.
  MmioWrite32 (mGicInterruptInterfaceBase + ARM_GIC_ICCICR, CpuControl);
  ArmDataSynchronizationBarrier ();
  MmioWrite32 (mGicDistributorBase + ARM_GIC_ICDICER, (1U << 27) | (1U << 30));
  for (Index = 0; Index < mGicNumInterrupts; Index++) {
    GicV2DisableInterruptSource (&gHardwareInterruptV2Protocol, Index);
  }
  ArmDataSynchronizationBarrier ();

  for (Index = 0; Index < 4096; Index++) {
    RawIar = (UINT32)ArmGicV2AcknowledgeInterrupt (mGicInterruptInterfaceBase);
    IntId = RawIar & 0x3FFU;
    if (IntId >= 1020 || IntId >= mGicNumInterrupts) {
      break;
    }
    // The protocol wrapper checks a bare INTID and rejects SGI CPU source
    // bits. Hardware EOIR must instead receive the complete original token.
    MmioWrite32 (mGicInterruptInterfaceBase + ARM_GIC_ICCEIOR, RawIar);
  }
  ArmDataSynchronizationBarrier ();
  // The board-profile helper clears Enable without changing other bits.
  ArmGicV2DisableInterruptInterface (mGicInterruptInterfaceBase);
  MmioAnd32 (mGicDistributorBase + ARM_GIC_ICDDCR, ~1U);
  ArmDataSynchronizationBarrier ();""")
    except (ValueError, IndexError):
        p.error("expected GicV2ExitBootServicesEvent implementation is missing or malformed")
    binary_inf = f"""# Generated external binary module; do not commit the binary.
[Defines]
  INF_VERSION = 0x0001001B
  BASE_NAME = B2LXnuEmbedded
  FILE_GUID = {guid}
  MODULE_TYPE = UEFI_APPLICATION
  VERSION_STRING = 0.1
[Binaries.AARCH64]
  PE32|XnuLoader.efi|*
"""
    dsc = dsc_path.read_text()
    pcd = "gPcBdsPkgTokenSpaceGuid.PcdShellFile|{ " + ", ".join(f"0x{b:02X}" for b in guid.bytes_le) + " }"
    dsc, n = re.subn(r"gPcBdsPkgTokenSpaceGuid\.PcdShellFile\|[^\r\n]*", pcd, dsc)
    if n != 1:
        p.error(f"expected exactly one PcdShellFile, found {n}")
    component = "beyond2ltePkg/B2LXnuEmbedded/B2LXnuEmbedded.inf"
    if component not in dsc:
        dsc += f"\n[Components]\n  {component}\n"
    fdf = fdf_path.read_text()
    if component not in fdf:
        if fdf.count("[FV.FVMAIN_COMPACT]") != 1:
            p.error("expected one compact FV declaration")
        fdf = fdf.replace("[FV.FVMAIN_COMPACT]", f"  INF {component}\n\n[FV.FVMAIN_COMPACT]")
    # XNU first boot consumes Apple DeviceTree built by its EFI loader.
    # ACPI remains outside this project and is not loaded by this firmware profile.
    for line in ("  INF MdeModulePkg/Universal/Acpi/AcpiTableDxe/AcpiTableDxe.inf",
                 "  INF MdeModulePkg/Universal/Acpi/AcpiPlatformDxe/AcpiPlatformDxe.inf",
                 "  !include Include/ACPI.inc"):
        fdf = fdf.replace(line, "  # B2L XNU profile omits ACPI")
    output = platform / "B2LXnuEmbedded"
    output.mkdir(exist_ok=True)
    shutil.copyfile(a.loader, output / "XnuLoader.efi")
    (output / "B2LXnuEmbedded.inf").write_text(binary_inf)
    dsc_path.write_text(dsc)
    fdf_path.write_text(fdf)
    manager_path.write_text(manager)
    manager_inf_path.write_text(manager_inf)
    gic_path.write_text(gic)
    memory_path.write_text(memory)
    ram_manager_path.write_text(ram_manager)
    print(f"Prepared external 4 MiB firmware profile at {root}")
    print(f"Embedded loader {guid}; post-console launch precedes BootNext/BootOrder and stops if the loader returns")


if __name__ == "__main__":
    main()
