/** @file
  Header file for RapidStart SMM S3 Handler.

@copyright
  Copyright (c) 2011 - 2013 Intel Corporation. All rights reserved
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
#ifndef _MM_MP_TEST_H_
#define _MM_MP_TEST_H_

#define    MM_MP_TEST_SW_SMI_VALUE      0xDE

typedef struct _PROCEDURE_ARGUMENTS {
  UINT32   ProcessorIndex;
  UINT32   MagicNumber;
  UINT32   SleepTime;
} PROCEDURE_ARGUMENTS;

#endif
