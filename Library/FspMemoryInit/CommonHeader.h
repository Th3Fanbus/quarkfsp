/** @file
Common header file shared by all source files.

Copyright (c) 2013 - 2016 Intel Corporation.

This program and the accompanying materials are licensed and made available
under the terms and conditions of the BSD License which accompanies this
distribution.  The full text of the license may be found at
http://opensource.org/licenses/bsd-license.php

THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.
**/

#ifndef __COMMON_HEADER_H_
#define __COMMON_HEADER_H_

#include <PiPei.h>
#include <IntelQNCPeim.h>
#include "Ioh.h"
#include <Platform.h>

#include <IndustryStandard/SmBus.h>
#include <IndustryStandard/Pci22.h>

#include <Guid/AcpiS3Context.h>
#include <Guid/MemoryTypeInformation.h>
#include <Guid/MemoryConfigData.h>
#include <Guid/MemoryOverwriteControl.h>
#include <Guid/SmramMemoryReserve.h>

#include <Library/DebugLib.h>
#include <Library/PeimEntryPoint.h>
#include <Library/BaseLib.h>
#include <Library/PeiServicesLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/HobLib.h>
#include <Library/PciCf8Lib.h>
#include <Library/IoLib.h>
#include <Library/PciLib.h>
#include <Library/ReportStatusCodeLib.h>
#include <Library/IntelQNCLib.h>
#include <Library/PcdLib.h>
#include <Library/SmbusLib.h>
#include <Library/TimerLib.h>
#include <Library/PrintLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PerformanceLib.h>
#include <Library/CacheMaintenanceLib.h>
#include <Library/MtrrLib.h>
#include <Library/QNCAccessLib.h>
#include <Library/FspCommonLib.h>
#include <Library/FspLib.h>
#include <Library/FspMemoryLib.h>

#include <Register/Cpuid.h>

#define FSP_STATUS_RESET_REQUIRED_COLD		0x40000001
#define FSP_STATUS_RESET_REQUIRED_WARM		0x40000002

#endif
