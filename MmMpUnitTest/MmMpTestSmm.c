/** @file
  This SMM driver will install a S3 Entry callback SMI handler for RTC wakeup policy.

@copyright
  Copyright (c) 2011 - 2014 Intel Corporation. All rights reserved
  This software and associated documentation (if any) is furnished
  under a license and may only be used or copied in accordance
  with the terms of the license. Except as permitted by such
  license, no part of this software or documentation may be
  reproduced, stored in a retrieval system, or transmitted in any
  form or by any means without the express written consent of
  Intel Corporation.
  This file contains an 'Intel Peripheral Driver' and uniquely
  identified as "Intel Mobile Silicon Support Module" and is
  licensed for Intel Mobile CPUs and chipsets under the terms of your
  license agreement with Intel or your vendor.  This file may
  be modified by the user, subject to additional terms of the
  license agreement
**/

///
/// External include files do NOT need to be explicitly specified in real EDKII
/// environment
///

#include <PiDxe.h>
#include <Protocol/SmmControl2.h>
#include <Protocol/SmmSwDispatch2.h>
#include <Protocol/MmMp.h>
#include <Protocol/SmmCpuService.h>

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/PciLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/SmmServicesTableLib.h>
#include <Library/TimerLib.h>
#include <Library/SynchronizationLib.h>

#include "MmMpTest.h"

SPIN_LOCK    mConsoleLock;

/**
  Calculate timeout value and return the current performance counter value.

  Calculate the number of performance counter ticks required for a timeout.
  If TimeoutInMicroseconds is 0, return value is also 0, which is recognized
  as infinity.

  @param[in]  TimeoutInMicroseconds   Timeout value in microseconds.
  @param[out] CurrentTime             Returns the current value of the performance counter.

  @return Expected time stamp counter for timeout.
          If TimeoutInMicroseconds is 0, return value is also 0, which is recognized
          as infinity.

**/
UINT64
CalculateTimeout (
  IN  UINTN   TimeoutInMicroseconds,
  OUT UINT64  *CurrentTime
  )
{
  UINT64 TimeoutInSeconds;
  UINT64 TimestampCounterFreq;

  //
  // Read the current value of the performance counter
  //
  *CurrentTime = GetPerformanceCounter ();

  //
  // If TimeoutInMicroseconds is 0, return value is also 0, which is recognized
  // as infinity.
  //
  if (TimeoutInMicroseconds == 0) {
    return 0;
  }

  //
  // GetPerformanceCounterProperties () returns the timestamp counter's frequency
  // in Hz. 
  //
  TimestampCounterFreq = GetPerformanceCounterProperties (NULL, NULL);

  //
  // Check the potential overflow before calculate the number of ticks for the timeout value.
  //
  if (DivU64x64Remainder (MAX_UINT64, TimeoutInMicroseconds, NULL) < TimestampCounterFreq) {
    //
    // Convert microseconds into seconds if direct multiplication overflows
    //
    TimeoutInSeconds = DivU64x32 (TimeoutInMicroseconds, 1000000);
    //
    // Assertion if the final tick count exceeds MAX_UINT64
    //
    ASSERT (DivU64x64Remainder (MAX_UINT64, TimeoutInSeconds, NULL) >= TimestampCounterFreq);
    return MultU64x64 (TimestampCounterFreq, TimeoutInSeconds);
  } else {
    //
    // No overflow case, multiply the return value with TimeoutInMicroseconds and then divide
    // it by 1,000,000, to get the number of ticks for the timeout value.
    //
    return DivU64x32 (
             MultU64x64 (
               TimestampCounterFreq,
               TimeoutInMicroseconds
               ),
             1000000
             );
  }
}

/**
  Checks whether timeout expires.

  Check whether the number of elapsed performance counter ticks required for
  a timeout condition has been reached.
  If Timeout is zero, which means infinity, return value is always FALSE.

  @param[in, out]  PreviousTime   On input,  the value of the performance counter
                                  when it was last read.
                                  On output, the current value of the performance
                                  counter
  @param[in]       TotalTime      The total amount of elapsed time in performance
                                  counter ticks.
  @param[in]       Timeout        The number of performance counter ticks required
                                  to reach a timeout condition.

  @retval TRUE                    A timeout condition has been reached.
  @retval FALSE                   A timeout condition has not been reached.

**/
BOOLEAN
CheckTimeout (
  IN OUT UINT64  *PreviousTime,
  IN     UINT64  *TotalTime,
  IN     UINT64  Timeout
  )
{
  UINT64  Start;
  UINT64  End;
  UINT64  CurrentTime;
  INT64   Delta;
  INT64   Cycle;

  if (Timeout == 0) {
    return FALSE;
  }
  GetPerformanceCounterProperties (&Start, &End);
  Cycle = End - Start;
  if (Cycle < 0) {
    Cycle = -Cycle;
  }
  Cycle++;
  CurrentTime = GetPerformanceCounter();
  Delta = (INT64) (CurrentTime - *PreviousTime);
  if (Start > End) {
    Delta = -Delta;
  }
  if (Delta < 0) {
    Delta += Cycle;
  }
  *TotalTime += Delta;
  *PreviousTime = CurrentTime;
  if (*TotalTime > Timeout) {
    return TRUE;
  }
  return FALSE;
}

VOID
Sleep (
  IN  UINTN   TimeoutInMicroseconds
  )
{
  UINT64 ExpectedTime;
  UINT64 CurrentTime;
  UINT64 TotalTime;

  ExpectedTime  = CalculateTimeout (
                       TimeoutInMicroseconds,
                       &CurrentTime
                       );

  while (!CheckTimeout (&CurrentTime, &TotalTime, ExpectedTime)) {
    CpuPause ();
  }
}

VOID
EFIAPI
DebugMsg (
  IN  UINTN        ErrorLevel,
  IN  CONST CHAR8  *Format,
  ...
  )
{
  VA_LIST         Marker;

  AcquireSpinLock (&mConsoleLock);

  VA_START (Marker, Format);
  DebugVPrint (ErrorLevel, Format, Marker);
  VA_END (Marker);

  ReleaseSpinLock (&mConsoleLock);
}

/**
  The function prototype for invoking a function on an Application Processor.

  This definition is used by the UEFI MP Serices Protocol, and the
  PI SMM System Table.

  @param[in,out] Buffer  The pointer to private data buffer.
**/

VOID
StartupProcedure (
  IN OUT VOID  *Buffer
  )
{
  PROCEDURE_ARGUMENTS            *Argument;
  
  Argument = (PROCEDURE_ARGUMENTS *)Buffer;

  DEBUG ((DEBUG_INFO, "    StartupProcedure Trigged, MagicNum = 0x%x, Processor Index = 0x%x!\n", Argument->MagicNumber, Argument->ProcessorIndex));
}


EFI_STATUS
SingleApSyncProcedure (
  IN VOID  *ProcedureArgument
  )
{
  PROCEDURE_ARGUMENTS            *Argument;
  
  Argument = (PROCEDURE_ARGUMENTS *)ProcedureArgument;

  DebugMsg (DEBUG_INFO, "    Ap Sync Procedure function done, MagicNum = 0x%x, Processor Index = 0x%x!\n", Argument->MagicNumber, Argument->ProcessorIndex);

  return Argument->MagicNumber;
}

EFI_STATUS
MultipleApSyncProcedure (
  IN VOID  *ProcedureArgument
  )
{
  PROCEDURE_ARGUMENTS            *Argument;
  
  Argument = (PROCEDURE_ARGUMENTS *)ProcedureArgument;

  DebugMsg (DEBUG_INFO, "    Ap Sync Procedure function done, MagicNum = 0x%x!\n", Argument->MagicNumber);

  return Argument->MagicNumber;
}

EFI_STATUS
SingleApAsyncProcedure (
  IN VOID  *ProcedureArgument
  )
{
  PROCEDURE_ARGUMENTS            *Argument;
  
  Argument = (PROCEDURE_ARGUMENTS *)ProcedureArgument;

  Sleep (Argument->SleepTime);
  
  DebugMsg (DEBUG_INFO, "    Ap Async Procedure function done, MagicNum = 0x%x, Processor Index = 0x%x!\n", Argument->MagicNumber, Argument->ProcessorIndex);

  return Argument->MagicNumber;
}

EFI_STATUS
MultipleApAsyncProcedure (
  IN VOID  *ProcedureArgument
  )
{
  PROCEDURE_ARGUMENTS            *Argument;
  
  Argument = (PROCEDURE_ARGUMENTS *)ProcedureArgument;

  Sleep (Argument->SleepTime);
  
  DebugMsg (DEBUG_INFO, "    Ap Async Procedure function done, MagicNum = 0x%x!\n", Argument->MagicNumber);

  return Argument->MagicNumber;
}

EFI_STATUS
SmmMpDispatchProcedureSyncModeVerification (
  IN EFI_MM_MP_PROTOCOL                *SmmMp,
  IN UINT32                             CpuNumber
  )
{
  EFI_STATUS                     Status;
  PROCEDURE_ARGUMENTS            Argument;
  EFI_STATUS                     ProcedureStatus;

  //
  // 1. Check block style. 
  // 
  Argument.MagicNumber    = 0x10;
  Argument.ProcessorIndex = CpuNumber;
  DEBUG ((DEBUG_INFO, "1.0 Block mode DispatchProcedure with CpuStatus == NULL\n"));
  DEBUG ((DEBUG_INFO, "1.0 Input Argument.MagicNumber = 0x%x.\n", Argument.MagicNumber));
  DEBUG ((DEBUG_INFO, "1.0 Input Argument.ProcessorIndex = 0x%x.\n", Argument.ProcessorIndex));

  Status = SmmMp->DispatchProcedure (SmmMp, SingleApSyncProcedure, CpuNumber, 0, &Argument, NULL, NULL);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "1.1 DispatchProcedure return status = %r.\n", Status));
    goto ErrorExit;
  } else {
    DEBUG ((DEBUG_ERROR, "1.1 DispatchProcedure return EFI_SUCCESS.\n"));
  }
  DEBUG ((DEBUG_ERROR, "\n"));

  DEBUG ((DEBUG_ERROR, "1.2 Block mode DispatchProcedure with CpuStatus != NULL.\n"));
  DEBUG ((DEBUG_INFO, "1.0 Input Argument.MagicNumber = 0x%x.\n", Argument.MagicNumber));
  DEBUG ((DEBUG_INFO, "1.0 Input Argument.ProcessorIndex = 0x%x.\n", Argument.ProcessorIndex));

  Status = SmmMp->DispatchProcedure (SmmMp, SingleApSyncProcedure, CpuNumber, 0, &Argument, NULL, &ProcedureStatus);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "1.2 DispatchProcedure return status = %r.\n", Status));
    goto ErrorExit;
  } else {
    DEBUG ((DEBUG_ERROR, "1.2 DispatchProcedure return EFI_SUCCESS.\n"));
  }

  //
  // Test Procedure return status.
  //
  DEBUG ((DEBUG_ERROR, "1.3 DispatchProcedure check Procedure return Status!\n"));
  if (ProcedureStatus != Argument.MagicNumber) {
    DEBUG ((DEBUG_ERROR, "1.3 DispatchProcedure check Procedure return status fail.\n"));
  } else {
    DEBUG ((DEBUG_ERROR, "1.3 DispatchProcedure check Procedure return status pass.\n"));
  }

ErrorExit:
  DEBUG ((DEBUG_ERROR, "\n"));

  return Status;
}


EFI_STATUS
SmmMpDispatchProcedureAsyncModeVerification (
  IN EFI_MM_MP_PROTOCOL                *SmmMp,
  IN UINT32                             CpuNumber,
  IN UINT32                            SleepNum,
  IN BOOLEAN                           WithStatus
  )
{
  MM_COMPLETION                  Token;
  EFI_STATUS                     Status;
  EFI_STATUS                     ProcedureStatus;
  PROCEDURE_ARGUMENTS            Argument;
  EFI_STATUS                     *ProcStatus;

  if (WithStatus) {
    DEBUG ((DEBUG_ERROR, "2.0 Non-Block mode DispatchProcedure with CpuStatus != NULL\n"));
    ProcStatus = &ProcedureStatus;
  } else {
    DEBUG ((DEBUG_ERROR, "2.0 Non-Block mode DispatchProcedure with CpuStatus == NULL\n"));
    ProcStatus = NULL;
  }

  //
  // 1. Check non-block style. 
  //
  Argument.MagicNumber = 0x20;
  Argument.SleepTime = SleepNum;
  Argument.ProcessorIndex = (UINT32) CpuNumber;

  DEBUG ((DEBUG_INFO, "2.0 Input Argument.MagicNumber = 0x%x!\n", Argument.MagicNumber));
  DEBUG ((DEBUG_INFO, "2.0 Input Argument.ProcessorIndex = 0x%x!\n", Argument.ProcessorIndex));
  DEBUG ((DEBUG_INFO, "2.0 Input Argument.SleepTime = 0x%x!\n", Argument.SleepTime));

  Status = SmmMp->DispatchProcedure (SmmMp, SingleApAsyncProcedure, CpuNumber, 0, &Argument, &Token, ProcStatus);
  if (EFI_ERROR (Status)) {
    DebugMsg (DEBUG_ERROR, "2.1 DispatchProcedure return status = %r\n", Status);
    goto Exit;
  } else {
    DebugMsg (DEBUG_ERROR, "2.1 DispatchProcedure function return EFI_SUCCESS!\n");
  }
  DebugMsg (DEBUG_ERROR, "\n");

  //
  // 2. check CheckForProcedure.
  //
  // DEBUG ((DEBUG_ERROR, "Token address = 0x%x!\n", &Token));
  DebugMsg (DEBUG_INFO, "2.2 Check For Procedure test begin.\n");
  Status = SmmMp->CheckForProcedure (SmmMp, Token);
  if (EFI_ERROR (Status)) {
    if (Status != EFI_NOT_READY) {
      DebugMsg (DEBUG_ERROR, "2.2 CheckForProcedure return status = %r!\n", Status);
      goto Exit;
    } else {
      DebugMsg (DEBUG_ERROR, "2.2 CheckForProcedure return EFI_NOT_READY!\n");
    }
  } else {
    DebugMsg (DEBUG_ERROR, "2.2 CheckForProcedure return EFI_SUCCESS!\n");
    goto Exit;
  }
  DebugMsg (DEBUG_ERROR, "\n");

  //
  // 3. check WaitForProcedure.
  //
  DebugMsg (DEBUG_INFO, "2.3 Wait For Procedure test begin.\n");
  Status = SmmMp->WaitForProcedure (SmmMp, Token);
  if (EFI_ERROR (Status)) {
    DebugMsg (DEBUG_ERROR, "2.3 SmmMpWaitForProcedure return status = %r!\n", Status);
  } else {
    DebugMsg (DEBUG_ERROR, "2.3 SmmMpWaitForProcedure return EFI_SUCCESS!\n");
  }

Exit:
  if (EFI_SUCCESS == Status && ProcStatus != NULL) {
    DebugMsg (DEBUG_INFO, "2.4 Check the procedure return status.\n");
    if (*ProcStatus == Argument.MagicNumber) {
      DebugMsg (DEBUG_INFO, "2.4 Check the procedure return status Pass.\n");
    } else {
      DebugMsg (DEBUG_INFO, "2.4 Check the procedure return status Fail.\n");
    }
  }
  DebugMsg (DEBUG_ERROR, "\n");

  return Status;
}

VOID
SmmMpDispatchProcedureVerification (
  IN EFI_MM_MP_PROTOCOL                 *SmmMp,
  IN UINT32                             CpuNumber
  )
{
  //
  // 1. Check block style. 
  // 
  SmmMpDispatchProcedureSyncModeVerification (SmmMp, CpuNumber);

  //
  // 2. Check non-block style with 0x80 sleep time. 
  // Expect WaitForProcedure should not work at this test.
  //
  SmmMpDispatchProcedureAsyncModeVerification (SmmMp, CpuNumber, 0x80, FALSE);

  //
  // 3. Check non-block style with 0x80 sleep time. 
  // Expect WaitForProcedure should not work at this test.
  //
  SmmMpDispatchProcedureAsyncModeVerification (SmmMp, CpuNumber, 0x80, TRUE);

  //
  // 4. Check non-block style with 0x400 sleep time.
  // Expect WaitForProcedure should work at this test.
  //
  SmmMpDispatchProcedureAsyncModeVerification (SmmMp, CpuNumber, 0x800, FALSE);

  //
  // 5. Check non-block style with 0x400 sleep time.
  // Expect WaitForProcedure should work at this test.
  //
  SmmMpDispatchProcedureAsyncModeVerification (SmmMp, CpuNumber, 0x800, TRUE);
}

EFI_STATUS
SmmMpBroadcastProcedureSyncModeVerification (
  IN EFI_MM_MP_PROTOCOL                 *SmmMp,
  IN UINT32                             ProcessorNum,
  IN BOOLEAN                            WithStatus
  )
{
  EFI_STATUS                     Status;
  EFI_STATUS                     *StatusArray;
  PROCEDURE_ARGUMENTS            Argument;
  UINTN                          Index;
  BOOLEAN                        FoundError;

  if (WithStatus) {
    DEBUG ((DEBUG_INFO, "3.0 Block Mode BroadcastProcedure test with CPUStatus != NULL\n"));
    StatusArray = AllocateZeroPool (sizeof(EFI_STATUS) * ProcessorNum);
    ASSERT (StatusArray != NULL);
    ZeroMem (StatusArray, sizeof(EFI_STATUS) * ProcessorNum);
  } else {
    DEBUG ((DEBUG_INFO, "3.0 Block Mode BroadcastProcedure test with CPUStatus == NULL\n"));
    StatusArray = NULL;
  }

  //
  // 1. Check block style. 
  // 
  Argument.MagicNumber    = 0x10;
  Argument.ProcessorIndex = (UINT32) ProcessorNum;
  DEBUG ((DEBUG_INFO, "3.0 Input Argument.MagicNumber = 0x%x!\n", Argument.MagicNumber));
  DEBUG ((DEBUG_INFO, "3.0 Input Argument.ProcessorIndex = 0x%x!\n", Argument.ProcessorIndex));

  //
  // 1. Check block style. 
  //
  Status = SmmMp->BroadcastProcedure (SmmMp, MultipleApSyncProcedure, 0, &Argument, NULL, StatusArray);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "3.1 BroadcastProcedure function return %r!\n", Status));
    goto ErrorExit;
  } else {
    DEBUG ((DEBUG_ERROR, "3.1 BroadcastProcedure function return EFI_SUCCESS!\n"));
  }

  if (WithStatus) {
    //
    // 2. Check procedure return status.
    //
    FoundError = FALSE;
    //
    // Skip BSP, Index begin from 1.
    //
    DEBUG ((DEBUG_ERROR, "3.2 BroadcastProcedure check Procedure return Status!\n"));
    for (Index = 1; Index < ProcessorNum; Index ++) {
      if (StatusArray[Index] != Argument.MagicNumber) {
        DEBUG ((DEBUG_ERROR, "3.2 BroadcastProcedure check procedure return status failed, Ap = 0x%x!\n", Index));
        FoundError = TRUE;
      }
    }
    
    if (!FoundError) {
      DEBUG ((DEBUG_ERROR, "3.2 BroadcastProcedure function check procedure return status pass!\n"));
    }
  }

ErrorExit:
  DEBUG ((DEBUG_ERROR, "\n"));

  return Status;
}


EFI_STATUS
SmmMpBroadcastProcedureAsyncModeVerification (
  IN EFI_MM_MP_PROTOCOL                 *SmmMp,
  IN UINT32                             ProcessorNum,
  IN UINT32                             SleepNum,
  IN BOOLEAN                            WithStatus
  )
{
  MM_COMPLETION                  Token;
  EFI_STATUS                     Status;
  EFI_STATUS                     *StatusArray;
  PROCEDURE_ARGUMENTS            Argument;
  BOOLEAN                        FoundError;
  UINTN                          Index;

  if (WithStatus) {
    DEBUG ((DEBUG_INFO, "4.0 Non-Block mode BroadcastProcedure test with CPUStatus != NULL\n"));
    StatusArray = AllocateZeroPool (sizeof(EFI_STATUS) * ProcessorNum);
    ASSERT (StatusArray != NULL);
    ZeroMem (StatusArray, sizeof(EFI_STATUS) * ProcessorNum);
  } else {
    DEBUG ((DEBUG_INFO, "4.0 Non-Block mode BroadcastProcedure test with CPUStatus == NULL\n"));
    StatusArray = NULL;
  }

  //
  // 1. Check Non-block style. 
  //
  Argument.ProcessorIndex = (UINT32) ProcessorNum;
  Argument.MagicNumber    = 0x20;
  Argument.SleepTime      = SleepNum;
  DEBUG ((DEBUG_INFO, "4.0 Input Argument.MagicNumber = 0x%x!\n", Argument.MagicNumber));
  DEBUG ((DEBUG_INFO, "4.0 Input Argument.ProcessorIndex = 0x%x!\n", Argument.ProcessorIndex));
  DEBUG ((DEBUG_INFO, "4.0 Input Argument.SleepTime = 0x%x!\n", Argument.SleepTime));

  Status = SmmMp->BroadcastProcedure (SmmMp, MultipleApAsyncProcedure, 0, &Argument, &Token, StatusArray);
  if (EFI_ERROR (Status)) {
    DebugMsg (DEBUG_ERROR, "4.1 BroadcastProcedure function return %r!\n", Status);
    goto Exit;
  } else {
    DebugMsg (DEBUG_ERROR, "4.1 BroadcastProcedure function return EFI_SUCCESS!\n");
  }
  DebugMsg (DEBUG_ERROR, "\n");

  DebugMsg (DEBUG_INFO, "4.2 Check For Procedure test begin.\n");
  Status = SmmMp->CheckForProcedure (SmmMp, Token);
  if (EFI_ERROR (Status)) {
    if (Status != EFI_NOT_READY) {
      DebugMsg (DEBUG_ERROR, "4.2 CheckForProcedure return status = %r!\n", Status);
      goto Exit;
    } else {
      DebugMsg (DEBUG_ERROR, "4.2 CheckForProcedure not get the final result! status = EFI_NOT_READY!\n");
    }
  } else {
    DebugMsg (DEBUG_ERROR, "4.2 CheckForProcedure get the final result! return EFI_SUCCESS!\n");
    goto Exit;
  }
  DebugMsg (DEBUG_ERROR, "\n");

  DebugMsg (DEBUG_INFO, "4.3 Wait For Procedure test begin.\n");
  Status = SmmMp->WaitForProcedure (SmmMp, Token);
  if (EFI_ERROR (Status)) {
    DebugMsg (DEBUG_ERROR, "4.3 WaitForProcedure return status = %r!\n", Status);
  } else {
    DebugMsg (DEBUG_ERROR, "4.3 WaitForProcedure get final result! return EFI_SUCCESS!\n");
  }

Exit:

  if (EFI_SUCCESS == Status && StatusArray != NULL) {
    FoundError = FALSE;
    //
    // Skip BSP, Index begin from 1.
    //
    DEBUG ((DEBUG_ERROR, "4.4 BroadcastProcedure check Procedure return Status!\n"));
    for (Index = 1; Index < ProcessorNum; Index ++) {
      if (StatusArray[Index] != Argument.MagicNumber) {
        DEBUG ((DEBUG_ERROR, "4.4 BroadcastProcedure check procedure return status failed, Ap = 0x%x!\n", Index));
        FoundError = TRUE;
      }
    }

    if (!FoundError) {
      DEBUG ((DEBUG_ERROR, "4.4 BroadcastProcedure check procedure return status pass!\n"));
    }
  }

  DEBUG ((DEBUG_ERROR, "\n"));

  return Status;
}


VOID
SmmMpBroadcastProcedureVerification (
  IN EFI_MM_MP_PROTOCOL                 *SmmMp,
  IN UINT32                             ProcessorNum
  )
{
  //
  // 1. Check block style. 
  //
  SmmMpBroadcastProcedureSyncModeVerification (SmmMp, ProcessorNum, FALSE);

  //
  // 1. Check block style. 
  //
  SmmMpBroadcastProcedureSyncModeVerification (SmmMp, ProcessorNum, TRUE);

  //
  // 2. Check Non-block style.
  // Expect WaitForProcedure should not work at this test.
  // 
  SmmMpBroadcastProcedureAsyncModeVerification (SmmMp, ProcessorNum, 0x80, FALSE);

  //
  // 2. Check Non-block style.
  // Expect WaitForProcedure should not work at this test.
  // 
  SmmMpBroadcastProcedureAsyncModeVerification (SmmMp, ProcessorNum, 0x80, TRUE);

  //
  // 3. Check Non-block style.
  // Expect WaitForProcedure should work at this test.
  // 
  SmmMpBroadcastProcedureAsyncModeVerification (SmmMp, ProcessorNum, 0x400, FALSE);

  //
  // 3. Check Non-block style.
  // Expect WaitForProcedure should work at this test.
  // 
  SmmMpBroadcastProcedureAsyncModeVerification (SmmMp, ProcessorNum, 0x400, TRUE);
}


EFI_STATUS
SmmMpVerification (
  VOID
  )
{
  EFI_STATUS                        Status;
  UINTN                             ProcessorsNum;
  EFI_MM_MP_PROTOCOL                *SmmMp;
  EFI_SMM_CPU_SERVICE_PROTOCOL      *SmmCpu;
  UINTN                             BspIndex;
  UINTN                             SelectedApIndex;
  PROCEDURE_ARGUMENTS               *Argument;

  Status = gSmst->SmmLocateProtocol (&gEfiMmMpProtocolGuid, NULL, (VOID **) &SmmMp);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "gEfiSmmMpProtocolGuid not found!\n"));
    return Status;
  }

  Status = gSmst->SmmLocateProtocol (&gEfiSmmCpuServiceProtocolGuid, NULL, (VOID **) &SmmCpu);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "gEfiSmmCpuServiceProtocolGuid not found!\n"));
    return Status;
  }

  DEBUG ((DEBUG_INFO, "\n"));
  DEBUG ((DEBUG_INFO, "0. Collect basic CPU information!\n"));
  Status = SmmCpu->WhoAmI (SmmCpu, &BspIndex);
  ASSERT_EFI_ERROR (Status);
  DEBUG ((DEBUG_INFO, "Bsp Index = %x!\n", BspIndex));

  //
  // 0. Test SmmMp->SetStartupProcedure API.
  //
  Argument = AllocatePool (sizeof (PROCEDURE_ARGUMENTS));
  ASSERT (Argument != NULL);
  Argument->ProcessorIndex = (UINT32) BspIndex;
  Argument->MagicNumber    = 0x1234;
  Status = SmmMp->SetStartupProcedure (SmmMp, StartupProcedure, Argument);
  ASSERT_EFI_ERROR (Status);
  DEBUG ((DEBUG_INFO, "Test for SmmMpSetStartupProcedure Done!\n"));

  //
  // 1. Test SmmMp->GetNumberOfProcessors API.
  //
  Status = SmmMp->GetNumberOfProcessors (SmmMp, &ProcessorsNum);
  ASSERT_EFI_ERROR (Status);
  DEBUG ((DEBUG_INFO, "SmmMpGetNumberOfProcessors return Processors Num = %x!\n", ProcessorsNum));
  DEBUG ((DEBUG_INFO, "Test for SmmMpGetNumberOfProcessors Done!\n"));

  if (ProcessorsNum != 1) {
    SelectedApIndex = ProcessorsNum - 1 != BspIndex ? ProcessorsNum - 1 : BspIndex - 1;
    DEBUG ((DEBUG_ERROR, "Selected Ap Index = %x to trig Smm Mp Dispatch Procedure!\n", SelectedApIndex));
  } else {
    DEBUG ((DEBUG_ERROR, "Only one processor found, can't do SMM MP protocol test.!\n"));
    return EFI_UNSUPPORTED;
  }
  DEBUG ((DEBUG_ERROR, "\n"));

  //
  // 2. Test SmmMp->DispatchProcedure API.
  //
  DEBUG ((DEBUG_INFO, "1. Begin to verify Dispatch Procedure!\n", Status));
  SmmMpDispatchProcedureVerification (SmmMp, (UINT32)SelectedApIndex);
  DebugMsg (DEBUG_ERROR, "\n");

  //
  // 3. Test SmmMp->BroadcastProcedure API.
  //
  DebugMsg (DEBUG_INFO, "2. Begin to verify Broadcast Procedure!\n", Status);
  SmmMpBroadcastProcedureVerification (SmmMp, (UINT32)ProcessorsNum);
  DebugMsg (DEBUG_ERROR, "\n");

  return Status;
}

/**
  A SW SMI callback to check whole memory CRC32

  @param[in] DispatchHandle  - The handle of this callback, obtained when registering
  @param[in] DispatchContext - Pointer to the EFI_SMM_SW_DISPATCH_CONTEXT
**/
EFI_STATUS
MmMpTestSwSmiCallback (
  IN  EFI_HANDLE                        DispatchHandle,
  IN  EFI_SMM_SW_REGISTER_CONTEXT       *DispatchContext,
  IN  OUT VOID                          *CommBuffer,
  IN  OUT UINTN                         *CommBufferSize
  )
{
  //CpuDeadLoop ();
  SmmMpVerification ();

  return EFI_SUCCESS;
}


/**
  Initializes the SMM S3 Handler.

  @param[in] ImageHandle - The image handle of Wake On Lan driver
  @param[in] SystemTable - The standard EFI system table

  @retval EFI_SUCCESS - RapidStart S3 entry callback SMI handle has been registered when RapidStart wake up policy enabled
**/
EFI_STATUS
EFIAPI
MmMpTestSmmEntryPoint (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_SMM_SW_DISPATCH2_PROTOCOL         *SwDispatch;
  EFI_SMM_SW_REGISTER_CONTEXT           SwContext;

  EFI_HANDLE                            DispatchHandle;
  EFI_STATUS                            Status;

  DEBUG ((DEBUG_INFO, "MmMpTestSmmEntryPoint()\n"));

  InitializeSpinLock((SPIN_LOCK*) &mConsoleLock);

  Status = gSmst->SmmLocateProtocol (&gEfiSmmSwDispatch2ProtocolGuid, NULL, (VOID**)&SwDispatch);
  ASSERT_EFI_ERROR (Status);

  ///
  /// Register SWSMI handler
  ///
  DEBUG ((DEBUG_INFO, "Register Mm Mp Test SwSmi: %x\n", MM_MP_TEST_SW_SMI_VALUE));
  SwContext.SwSmiInputValue = MM_MP_TEST_SW_SMI_VALUE;

  Status = SwDispatch->Register (SwDispatch,
                                 (EFI_SMM_HANDLER_ENTRY_POINT2)MmMpTestSwSmiCallback,
                                 &SwContext,
                                 &DispatchHandle);

  ASSERT_EFI_ERROR (Status);

  return EFI_SUCCESS;
}
