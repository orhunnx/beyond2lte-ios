/* SPDX-License-Identifier: BSD-2-Clause
 * Read-only SYSTEM cache staging. USERDATA is never written by firmware;
 * XNU owns journal replay and all original-file updates before init starts.
 */
#include "b2l_cache_core.h"
#include "BootPayload.h"
#include "BootDevicePath.h"
#include <Protocol/BlockIo.h>
#include <Protocol/DiskIo.h>
#include <Protocol/PartitionInfo.h>
#include <Protocol/DevicePath.h>
#include <Library/DevicePathLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
struct disk_reader {EFI_DISK_IO_PROTOCOL *disk;UINT32 media;UINT64 bytes;};
static int read_disk(void*v,uint64_t off,void*out,size_t n){struct disk_reader*r=v;
 if(off>r->bytes||n>r->bytes-off)return -1;
 return EFI_ERROR(r->disk->ReadDisk(r->disk,r->media,off,n,out))?-1:0;}
static int label_equal(const CHAR16*p,const CHAR16*s){
 for(unsigned i=0;i<36;i++){if(p[i]!=s[i])return 0;if(!s[i])return 1;}return 0;
}
static EFI_STATUS partition(const CHAR16*upper,const CHAR16*lower,struct disk_reader*out){
 EFI_HANDLE*handles=NULL;UINTN count=0;UINT32 boot_lun;
 if(EFI_ERROR(B2lGetBootUfsLun(&boot_lun)))return EFI_NOT_FOUND;
 EFI_STATUS status=gBS->LocateHandleBuffer(ByProtocol,&gEfiBlockIoProtocolGuid,NULL,&count,&handles);
 if(EFI_ERROR(status))return status;unsigned found=0;
 for(UINTN i=0;i<count;i++){
  EFI_BLOCK_IO_PROTOCOL*b;EFI_PARTITION_INFO_PROTOCOL*p;EFI_DEVICE_PATH_PROTOCOL*path;EFI_DISK_IO_PROTOCOL*d;UINT32 lun;
  if(EFI_ERROR(gBS->HandleProtocol(handles[i],&gEfiBlockIoProtocolGuid,(VOID**)&b))||!b->Media||!b->Media->MediaPresent||!b->Media->LogicalPartition||
    EFI_ERROR(gBS->HandleProtocol(handles[i],&gEfiPartitionInfoProtocolGuid,(VOID**)&p))||p->Type!=PARTITION_TYPE_GPT||
    (!label_equal(p->Info.Gpt.PartitionName,upper)&&!label_equal(p->Info.Gpt.PartitionName,lower))||
    EFI_ERROR(gBS->HandleProtocol(handles[i],&gEfiDevicePathProtocolGuid,(VOID**)&path))||
    b2l_boot_ufs_lun(path,GetDevicePathSize(path),&lun)||lun!=boot_lun||
    EFI_ERROR(gBS->HandleProtocol(handles[i],&gEfiDiskIoProtocolGuid,(VOID**)&d))||!b->Media->BlockSize||
    b->Media->LastBlock==MAX_UINT64||b->Media->LastBlock+1>MAX_UINT64/b->Media->BlockSize)continue;
  found++;out->disk=d;out->media=b->Media->MediaId;out->bytes=(b->Media->LastBlock+1)*b->Media->BlockSize;
 }
 FreePool(handles);return found==1?EFI_SUCCESS:EFI_NOT_FOUND;
}
UINTN B2lStageNativeCache(VOID*destination,UINTN capacity){
 struct disk_reader system,root;uint8_t vh[512],id[12],headers[8192];struct b2l_cache_fork fork;struct b2l_io view;
 if(capacity<B2L_NC_HEADER||EFI_ERROR(partition(L"SYSTEM",L"system",&system))||
    EFI_ERROR(partition(L"USERDATA",L"userdata",&root)))return 0;
 if(read_disk(&root,1024,vh,sizeof(vh))||vh[0]!='H'||(vh[1]!='+'&&vh[1]!='X'))return 0;
 CopyMem(id,vh+104,8);CopyMem(id+8,vh+16,4);
 struct b2l_hfs*h=AllocateZeroPool(sizeof(*h));struct b2l_hfs_file file;
 if(!h)return 0;
 struct b2l_io io={read_disk,NULL,NULL,&system,system.bytes};
 int error=b2l_hfs_open(h,&io);
 if(!error)error=b2l_hfs_find(h,"b2l-cache.bin",&file);
 if(!error)error=b2l_hfs_cache_view(h,&file,&fork,&view);
 if(!error)error=view.read(view.ctx,0,headers,sizeof(headers));
 const uint8_t*selected=NULL;
 if(!error)error=b2l_nc_headers(headers,id,file.data.size,&selected);
 if(!error)error=view.read(view.ctx,0,destination,B2L_NC_HEADER);
 FreePool(h);
 if(error){Print(L"B2L cache: absent or invalid; normal emulation retained.\r\n");return 0;}
 Print(L"B2L cache: staged 8 KiB headers; disk capacity %lu MiB; XNU streams records.\r\n",file.data.size/(1024*1024));
 return B2L_NC_HEADER;
}
