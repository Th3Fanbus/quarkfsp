/** @file
FspMemoryInit function prototype definitions.

Copyright (c) 2016 Intel Corporation.

This program and the accompanying materials are licensed and made available
under the terms and conditions of the BSD License which accompanies this
distribution.  The full text of the license may be found at
http://opensource.org/licenses/bsd-license.php

THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

**/

#ifndef __FSP_MEMORY_INIT_H__
#define __FSP_MEMORY_INIT_H__

#include <Pi/PiBootMode.h>

typedef struct _FSP_STACK_DATA {
  UINT8 junk;
} FSP_STACK_DATA;

/**
FspMemoryInit: Initialize DRAM for caller

@param  StackData    Pointer to the global FSP stack data structure.

@return EFI_SUCCESS  Memory initialization completed successfully.
                     Other error conditions are possible.
**/
EFI_STATUS
FspMemoryInit(
IN  FSP_STACK_DATA *StackData
);

#endif // __FSP_MEMORY_INIT_H__
