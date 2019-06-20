/** @file

Copyright (c) 2017, Intel Corporation. All rights reserved.<BR>
This program and the accompanying materials
are licensed and made available under the terms and conditions of the BSD License
which accompanies this distribution.  The full text of the license may be found at
http://opensource.org/licenses/bsd-license.php

THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

**/

#include <PiDxe.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/DxeServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>

#include <MmMpTest.h>


EFI_STATUS
EFIAPI
InitializeSmiPerf (
  IN EFI_HANDLE           ImageHandle,
  IN EFI_SYSTEM_TABLE     *SystemTable
  )
{
  EFI_TPL                   OldTpl;

  Print (L"Trig SMI to test Mm Mp Protocol Begin!\n");

  OldTpl = gBS->RaiseTPL (TPL_HIGH_LEVEL);
  IoWrite8 (0xB2, MM_MP_TEST_SW_SMI_VALUE);
  gBS->RestoreTPL (OldTpl);

  Print (L"Trig SMI to test Mm Mp Protocol Done!\n");

  return EFI_SUCCESS;
}
