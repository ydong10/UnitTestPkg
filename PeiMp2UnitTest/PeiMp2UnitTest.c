/** @file
  This driver installs Single Segment Pci Configuration 2 PPI
  to provide read, write and modify access to Pci configuration space in PEI phase.
  To follow PI specification, these services also support access to the unaligned Pci address.

  Copyright (c) 2006 - 2018, Intel Corporation. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <PiPei.h>
#include <Ppi/MpServices2.h>
#include <Library/PeiServicesLib.h>
#include <Library/DebugLib.h>
#include <Library/TimerLib.h>
#include <Library/SynchronizationLib.h>

EDKII_PEI_MP_SERVICES2_PPI           *mCpuMp2Ppi;

typedef struct {
  UINT32    MagicNum;
  UINT32    SleepTime;
} PEI_MP2_PROCEDURE_PARAM;

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
Procedure (
  IN VOID  *ProcedureArgument
  )
{
  PEI_MP2_PROCEDURE_PARAM            *Argument;
  UINTN                              ApIndex;

  ApIndex = 0;

  EFI_STATUS                         Status;

  if (mCpuMp2Ppi != NULL) {
    Status = mCpuMp2Ppi->WhoAmI (mCpuMp2Ppi, &ApIndex);
    ASSERT_EFI_ERROR (Status);
  }

  Argument = (PEI_MP2_PROCEDURE_PARAM *)ProcedureArgument;
  while (!AcquireSpinLockOrFail (&mConsoleLock)) {
    CpuPause ();
  }

  DEBUG((DEBUG_INFO, "Ap 0x%x Procedure function done, SleepTime = 0x%x.\n", ApIndex, Argument->SleepTime));
  ReleaseSpinLock (&mConsoleLock);

  if (Argument->SleepTime != 0) {
    Sleep (Argument->SleepTime);
  }
}

VOID
TestAPIEnableDisableAP (
  VOID
  )
{
  UINTN                       NumberOfProcessors;
  UINTN                       NumberOfEnabledProcessors;
  EFI_STATUS                  Status;
  PEI_MP2_PROCEDURE_PARAM     ProcParam;

  DEBUG((DEBUG_INFO, "1.Test EnableDisableAP begin!\n"));
  Status = mCpuMp2Ppi->GetNumberOfProcessors (
                         mCpuMp2Ppi,
                         &NumberOfProcessors,
                         &NumberOfEnabledProcessors);
  if (EFI_ERROR (Status)) {
    ///
    /// If unable to retrieve the Number of Processors - assert.
    ///
    DEBUG((DEBUG_INFO, "GetNumberOfProcessors return failure!\n"));
    return;
  }
  DEBUG((DEBUG_INFO, "Before disable one AP, NumOfProc = 0x%x, NumOfEnableProc = 0x%x!\n", NumberOfProcessors, NumberOfEnabledProcessors));

  Status = mCpuMp2Ppi->EnableDisableAP (
                        mCpuMp2Ppi,
                        NumberOfProcessors - 1,
                        FALSE,
                        NULL
                        );
  if (EFI_ERROR (Status)) {
    ///
    /// If unable to retrieve the Number of Processors - assert.
    ///
    DEBUG((DEBUG_INFO, "EnableDisableAP return failure!\n"));
    return;
  }

  Status = mCpuMp2Ppi->GetNumberOfProcessors (
                         mCpuMp2Ppi,
                         &NumberOfProcessors,
                         &NumberOfEnabledProcessors);
  ASSERT_EFI_ERROR (Status);
  DEBUG((DEBUG_INFO, "After disable one AP, NumOfProc = 0x%x, NumOfEnableProc = 0x%x!\n", NumberOfProcessors, NumberOfEnabledProcessors));

  Status = mCpuMp2Ppi->EnableDisableAP (
                        mCpuMp2Ppi,
                        NumberOfProcessors - 1,
                        TRUE,
                        NULL
                        );
  ASSERT_EFI_ERROR (Status);

  Status = mCpuMp2Ppi->GetNumberOfProcessors (
                         mCpuMp2Ppi,
                         &NumberOfProcessors,
                         &NumberOfEnabledProcessors);
  ASSERT_EFI_ERROR (Status);

  DEBUG((DEBUG_INFO, "After enable one AP, NumOfProc = 0x%x, NumOfEnableProc = 0x%x!\n", NumberOfProcessors, NumberOfEnabledProcessors));

  //
  // Add below code here to test an regression issue fix below bugz:
  // https://bugzilla.tianocore.org/show_bug.cgi?id=2474
  //
  ProcParam.SleepTime = 0x30;
  DEBUG((DEBUG_INFO, "Trig StartupAllCPUs with SleepTime = 0x%x\n", ProcParam.SleepTime));
  Status = mCpuMp2Ppi->StartupAllCPUs (
                 mCpuMp2Ppi,
                 Procedure,
                 0x20,
                 &ProcParam
                 );
  if (EFI_ERROR (Status)) {
    //
    // StartAllCPUs will let APs to run the procedure first, then BSP
    // itself will run the procedure, at last BSP will check the APs
    // result. So the timeout value not works here because Bsp run
    // procedure also needs time.
    //
    DEBUG((DEBUG_INFO, "Trig StartupAllCPUs returns Fail !\n"));
  } else {
    DEBUG((DEBUG_INFO, "Trig StartupAllCPUs returns Pass !\n"));
  }
}

VOID
TestAPIStartAllCPU (
  VOID
  )
{
  EFI_STATUS                           Status;
  PEI_MP2_PROCEDURE_PARAM              ProcParam;

  ProcParam.SleepTime = 0;
  DEBUG((DEBUG_INFO, "1.Test StartupAllCPUs begin, SleepTime = 0x%x\n", ProcParam.SleepTime));
  Status = mCpuMp2Ppi->StartupAllCPUs (
                 mCpuMp2Ppi,
                 Procedure,
                 0,
                 &ProcParam
                 );
  if (EFI_ERROR (Status)) {
    DEBUG((DEBUG_INFO, "1. Test StartupAllCPUs End ==== Fail !\n"));
  } else {
    DEBUG((DEBUG_INFO, "1. Test StartupAllCPUs End ==== Pass !\n"));
  }

  ProcParam.SleepTime = 0x30;
  DEBUG((DEBUG_INFO, "2.Test StartupAllCPUs with SleepTime = 0x%x\n", ProcParam.SleepTime));
  Status = mCpuMp2Ppi->StartupAllCPUs (
                 mCpuMp2Ppi,
                 Procedure,
                 0x20,
                 &ProcParam
                 );
  if (EFI_ERROR (Status)) {
    //
    // StartAllCPUs will let APs to run the procedure first, then BSP
    // itself will run the procedure, at last BSP will check the APs
    // result. So the timeout value not works here because Bsp run
    // procedure also needs time.
    //
    DEBUG((DEBUG_INFO, "2. Test StartupAllCPUs End ==== Fail !\n"));
  } else {
    DEBUG((DEBUG_INFO, "2. Test StartupAllCPUs End ==== Pass !\n"));
  }
}

/**
  Module's entry function.
  This routine will install EFI_PEI_PCI_CFG2_PPI.

  @param  FileHandle  Handle of the file being invoked.
  @param  PeiServices Describes the list of possible PEI Services.

  @return Whether success to install service.
**/
EFI_STATUS
EFIAPI
PeiMp2UnitTest (
  IN       EFI_PEI_FILE_HANDLE  FileHandle,
  IN CONST EFI_PEI_SERVICES     **PeiServices
  )
{
  EFI_STATUS                           Status;

  InitializeSpinLock((SPIN_LOCK*) &mConsoleLock);

  //
  // Get MP Services2 Ppi
  //
  Status = PeiServicesLocatePpi (
             &gEdkiiPeiMpServices2PpiGuid,
             0,
             NULL,
             (VOID **)&mCpuMp2Ppi
             );
  ASSERT_EFI_ERROR (Status);

  DEBUG((DEBUG_INFO, "=========================================\n"));
  DEBUG((DEBUG_INFO, "Begin do Edkii Pei Mp Services2 Ppi test!\n"));

  TestAPIStartAllCPU ();

  TestAPIEnableDisableAP ();

  DEBUG((DEBUG_INFO, "Edkii Pei Mp Services2 Ppi test End!\n"));
  DEBUG((DEBUG_INFO, "=========================================\n"));

  return EFI_SUCCESS;
}
