// SPDX-License-Identifier: BSD-2-Clause-Patent
#ifndef B2L_BOOT_PAYLOAD_H
#define B2L_BOOT_PAYLOAD_H
#include <Uefi.h>
#define B2L_BOOT_SEED_SIZE 256U

// Returns an AllocatePool-owned Mach-O buffer from the current BOOT image.
// This path only reads storage. It never opens a device for writing.
EFI_STATUS B2lLoadBootPayload(VOID **Kernel, UINTN *KernelSize, UINT8 Seed[B2L_BOOT_SEED_SIZE], UINT8 WifiMac[6]);
// Public LUN identity from the actual loaded BOOT partition's parent path.
// Returns NOT_FOUND for filesystem fallback or an unrecognized controller.
EFI_STATUS B2lGetBootUfsLun(UINT32 *Lun);
#endif
