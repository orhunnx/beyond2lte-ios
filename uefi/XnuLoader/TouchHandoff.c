/* SPDX-License-Identifier: BSD-2-Clause */
#include <Uefi.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/BaseMemoryLib.h>
#include "TouchDiagnostic.h"
/* Versioned compact snapshot; physical addresses remain informational. */
VOID B2lTouchHandoff(UINT64 out[24])
{
    EFI_GUID guid={0x47c2c225,0x58af,0x47aa,{0xb7,0xf0,0xbc,0x15,0xd4,0x34,0xfa,0x94}};
    B2L_TOUCH_DIAGNOSTIC_PROTOCOL *p=NULL;
    B2L_TOUCH_DIAGNOSTIC_DATA d;
    ZeroMem(out,24*sizeof(UINT64));out[0]=1;
    EFI_STATUS s=gBS->LocateProtocol(&guid,NULL,(VOID**)&p);out[1]=s;
    if(EFI_ERROR(s))return;
    if(!p||p->Revision!=B2L_TOUCH_DIAGNOSTIC_REVISION||!p->GetDiagnostic){out[1]=EFI_INCOMPATIBLE_VERSION;return;}
    ZeroMem(&d,sizeof(d));s=p->GetDiagnostic(p,&d);out[1]=s;
    if(EFI_ERROR(s)||d.Revision!=B2L_TOUCH_DIAGNOSTIC_REVISION)return;
    out[2]=d.InitStatus;out[3]=d.ProbeStatus;out[4]=d.ResetStatus;
    out[5]=d.TouchFunctionStatus;out[6]=d.PointerStatus;
    out[7]=d.PowerDvddStatus;out[8]=d.PowerAvddStatus;out[9]=d.BootStatus;
    out[10]=d.TimingFs1;out[11]=d.TimingFs2;out[12]=d.TimingFs3;out[13]=d.TimingSla;
    out[14]=d.UsiMode;out[15]=d.Ctrl;out[16]=d.LastIntStatusRaw;
    out[17]=d.LastErrorStatusRaw;out[18]=d.LastTransferStatusRaw;out[19]=d.LastFifoStatusRaw;
    out[20]=d.TransferCount;out[21]=d.TransferFailureCount;
    out[22]=d.FirmwareIntegrity;out[23]=d.Address;
}
