/* Protocol ABI from UEFI_beyond2lte touch branch 48add5b. */
#ifndef B2L_TOUCH_DIAGNOSTIC_PROTOCOL_H
#define B2L_TOUCH_DIAGNOSTIC_PROTOCOL_H

#include <Uefi.h>

#define B2L_TOUCH_DIAGNOSTIC_REVISION  5
#define B2L_TOUCH_DIAGNOSTIC_READ_MAX  32
#define B2L_TOUCH_EVENT_SIZE           8

typedef struct {
  UINT32      Revision;
  UINT8       Bus;
  UINT8       Slave;
  UINT8       LastCommand;
  UINT8       LastReadLength;
  UINT64      ControllerAddress;
  UINT64      UsiAddress;
  EFI_STATUS  InitStatus;
  EFI_STATUS  PowerProtocolStatus;
  EFI_STATUS  PowerDvddStatus;
  EFI_STATUS  PowerAvddStatus;
  EFI_STATUS  ProbeStatus;
  EFI_STATUS  ResetStatus;
  EFI_STATUS  TouchFunctionStatus;
  EFI_STATUS  PointerStatus;
  EFI_STATUS  LastPollStatus;
  EFI_STATUS  LastTransferStatus;
  UINT64      TransferCount;
  UINT64      TransferSuccessCount;
  UINT64      TransferFailureCount;
  UINT64      PollCount;
  UINT64      RawEventCount;
  UINT64      ParsedEventCount;
  UINT64      PollFailureCount;
  UINT64      EventTypeCount[4];
  UINT32      PollErrorCount;
  BOOLEAN     Disabled;
  BOOLEAN     ExitingBootServices;
  UINT8       DeviceId[5];
  UINT8       FirmwareIntegrity;
  UINT8       BootStatus;
  UINT16      TouchFunctions;
  UINT8       LastEvent[B2L_TOUCH_EVENT_SIZE];
  UINT8       LastRead[B2L_TOUCH_DIAGNOSTIC_READ_MAX];
  UINT32      UsiMode;
  UINT32      Ctrl;
  UINT32      FifoCtrl;
  UINT32      ClockControl;
  UINT32      ClockSlot;
  UINT32      IntEnable;
  UINT32      IntStatus;
  UINT32      ErrorStatus;
  UINT32      FifoStatus;
  UINT32      Conf;
  UINT32      AutoConf;
  UINT32      Command;
  UINT32      TransferStatus;
  UINT32      Address;
  UINT32      TimingHs1;
  UINT32      TimingHs2;
  UINT32      TimingHs3;
  UINT32      TimingFs1;
  UINT32      TimingFs2;
  UINT32      TimingFs3;
  UINT32      TimingSla;
  UINT32      LastIntStatusRaw;
  UINT32      LastErrorStatusRaw;
  UINT32      LastTransferStatusRaw;
  UINT32      LastFifoStatusRaw;
  EFI_STATUS  SclStatus;
  EFI_STATUS  SdaStatus;
  EFI_STATUS  InitialSclStatus;
  EFI_STATUS  InitialSdaStatus;
  BOOLEAN     SclHigh;
  BOOLEAN     SdaHigh;
  BOOLEAN     InitialSclHigh;
  BOOLEAN     InitialSdaHigh;
} B2L_TOUCH_DIAGNOSTIC_DATA;

typedef struct _B2L_TOUCH_DIAGNOSTIC_PROTOCOL B2L_TOUCH_DIAGNOSTIC_PROTOCOL;

typedef
EFI_STATUS
(EFIAPI *B2L_TOUCH_GET_DIAGNOSTIC)(
  IN  B2L_TOUCH_DIAGNOSTIC_PROTOCOL  *This,
  OUT B2L_TOUCH_DIAGNOSTIC_DATA      *Data
  );

struct _B2L_TOUCH_DIAGNOSTIC_PROTOCOL {
  UINT32                    Revision;
  B2L_TOUCH_GET_DIAGNOSTIC  GetDiagnostic;
};

extern EFI_GUID  gB2LTouchDiagnosticProtocolGuid;

#endif
