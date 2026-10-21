// SPDX-License-Identifier: BSD-2-Clause-Patent
#ifndef B2L_BOOT_PAYLOAD_H
#define B2L_BOOT_PAYLOAD_H
#include <Uefi.h>

// Returns an AllocatePool-owned Mach-O buffer from the current BOOT image.
// This path only reads storage. It never opens a device for writing.
EFI_STATUS B2lLoadBootPayload(VOID **Kernel, UINTN *KernelSize, UINT8 Seed[64]);
#endif
