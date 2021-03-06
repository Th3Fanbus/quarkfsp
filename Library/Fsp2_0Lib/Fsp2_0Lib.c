/** @file
  FSP 2.0 support library functions

  Copyright (c) 2016, Intel Corporation. All rights reserved.<BR>
  This program and the accompanying materials are licensed and made available
  under the terms and conditions of the BSD License which accompanies this
  distribution.  The full text of the license may be found at
  http://opensource.org/licenses/bsd-license.php

  THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
  WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

**/

#include <PiPei.h>
#include <Library/DebugLib.h>
#include <Library/FspLib.h>
#include <Library/FspMemoryLib.h>
#include <Library/FspMemoryInit.h>
#include <Library/HobLib.h>
#include <Library/QNCAccessLib.h>
#include <Pi/PiBootMode.h>
#include <Pi/PiHob.h>
#include <FspmUpd.h>
#include <BootLoaderPlatformData.h>
#include "StackData.h"

#define SSKPD0			0x4a

VOID
BuildFspSmbiosMemoryInfoHob (
  UINT8  MemoryType,
  UINT16 MemoryFrequencyInMHz,
  UINT32 SizeInMb,
  UINT16 DataWidth,
  UINT8  ErrorCorrectionType,
  UINT32 NumChannels,
  UINT32 MaxSockets,
  EFI_GUID *HobGuid
  )
{
  FSP_SMBIOS_MEMORY_INFO      FspSmbiosMemoryInfo;
  UINT8                       ChannelIndex;
  UINT8                       ChannelCount;
  UINT8                       DimmIndex;
  UINT8                       DimmCount;

  FspSmbiosMemoryInfo.Revision = 0x01;
  FspSmbiosMemoryInfo.MemoryType = MemoryType;
  FspSmbiosMemoryInfo.MemoryFrequencyInMHz = MemoryFrequencyInMHz;
  FspSmbiosMemoryInfo.ErrorCorrectionType = ErrorCorrectionType;

  ChannelCount = 0;
  for (ChannelIndex = 0; ChannelIndex < NumChannels; ChannelIndex++) {
    DimmCount = 0;
    FspSmbiosMemoryInfo.ChannelInfo[ChannelIndex].ChannelId = ChannelIndex;
    for (DimmIndex = 0; DimmIndex < MaxSockets; DimmIndex++) {
      FspSmbiosMemoryInfo.ChannelInfo[ChannelIndex].DimmInfo[DimmIndex].DimmId = DimmIndex;
      FspSmbiosMemoryInfo.ChannelInfo[ChannelIndex].DimmInfo[DimmIndex].SizeInMb = SizeInMb;
        ///
        /// Dimm is present in slot
        /// Get the Memory DataWidth info
        /// SPD Offset 8 Bits [2:0] DataWidth aka Primary Bus Width
        ///
        FspSmbiosMemoryInfo.DataWidth = DataWidth;
      DimmCount++;
    }
    FspSmbiosMemoryInfo.ChannelInfo[ChannelIndex].DimmCount = DimmCount;
    ChannelCount++;
  }
  FspSmbiosMemoryInfo.ChannelCount = ChannelCount;

  //
  // Build HOB for FspSmbiosMemoryInfo
  //
  BuildGuidDataHob (
    HobGuid,
    &FspSmbiosMemoryInfo,
    sizeof (FSP_SMBIOS_MEMORY_INFO)
    );
}

VOID *FspGetHobList (VOID)
{
  FSP_STACK_DATA *StackData;

  StackData = GetStackData();
  ASSERT (StackData->HobList != NULL);
  return StackData->HobList;
}

UINT32 GetBootLoaderTolumSize(VOID)
{
  FSPM_UPD *FspmUpd;
  FSP_STACK_DATA *StackData;

  //
  // Get the UPD pointer
  //
  StackData = GetStackData();
  FspmUpd = StackData->Upd;
  ASSERT (FspmUpd != NULL);

  //
  // Return the requested value
  //
  return FspmUpd->FspmArchUpd.BootLoaderTolumSize;
}

UINT32 GetBootMode(VOID)
{
  FSP_STACK_DATA *StackData;

  StackData = GetStackData();
  ASSERT (StackData != NULL);
  return StackData->Upd->FspmArchUpd.BootMode;
}

UINT32 GetEccScrubBlkSize(VOID)
{
  FSPM_UPD *FspmUpd;
  FSP_STACK_DATA *StackData;

  //
  // Get the UPD pointer
  //
  StackData = GetStackData();
  FspmUpd = StackData->Upd;
  ASSERT (FspmUpd != NULL);

  //
  // Return the requested value
  //
  return FspmUpd->FspmConfig.EccScrubBlkSize;
}

UINT32 GetEccScrubInterval(VOID)
{
  FSPM_UPD *FspmUpd;
  FSP_STACK_DATA *StackData;

  //
  // Get the UPD pointer
  //
  StackData = GetStackData();
  FspmUpd = StackData->Upd;
  ASSERT (FspmUpd != NULL);

  //
  // Return the requested value
  //
  return FspmUpd->FspmConfig.EccScrubInterval;
}

UINT32 GetFspReservedMemoryLength(VOID)
{
  FSPM_UPD *FspmUpd;
  FSP_STACK_DATA *StackData;

  //
  // Get the UPD pointer
  //
  StackData = GetStackData();
  FspmUpd = StackData->Upd;
  ASSERT (FspmUpd != NULL);

  //
  // Return the requested value
  //
  return FspmUpd->FspmConfig.FspReservedMemoryLength;
}

VOID GetMemoryParameters (
  OUT MRC_PARAMS  *MrcData
  )
{
  FSPM_UPD *FspmUpd;
  FSP_STACK_DATA *StackData;

  //
  // Get the UPD pointer
  //
  StackData = GetStackData();
  FspmUpd = StackData->Upd;
  ASSERT (FspmUpd != NULL);

  //
  // Initialize the MRC data
  //
  MrcData->channel_enables     = FspmUpd->FspmConfig.ChanMask;
  MrcData->channel_width       = FspmUpd->FspmConfig.ChanWidth;
  MrcData->address_mode        = FspmUpd->FspmConfig.AddrMode;
  // Enable scrambling if requested.
  MrcData->scrambling_enables  = (UINT8)FspmUpd->FspmConfig.Flags;
  MrcData->ddr_type            = FspmUpd->FspmConfig.DramType;
  MrcData->dram_width          = FspmUpd->FspmConfig.DramWidth;
  MrcData->ddr_speed           = FspmUpd->FspmConfig.DramSpeed;
  // Enable ECC if requested.
  MrcData->rank_enables        = FspmUpd->FspmConfig.RankMask;
  MrcData->params.DENSITY      = FspmUpd->FspmConfig.DramDensity;
  MrcData->params.tCL          = FspmUpd->FspmConfig.tCL;
  MrcData->params.tRAS         = FspmUpd->FspmConfig.tRAS;
  MrcData->params.tWTR         = FspmUpd->FspmConfig.tWTR;
  MrcData->params.tRRD         = FspmUpd->FspmConfig.tRRD;
  MrcData->params.tFAW         = FspmUpd->FspmConfig.tFAW;

  MrcData->refresh_rate        = FspmUpd->FspmConfig.SrInt;
  MrcData->sr_temp_range       = FspmUpd->FspmConfig.SrTemp;
  MrcData->ron_value           = FspmUpd->FspmConfig.DramRonVal;
  MrcData->rtt_nom_value       = FspmUpd->FspmConfig.DramRttNomVal;
  MrcData->rd_odt_value        = FspmUpd->FspmConfig.SocRdOdtVal;
}

UINT32 GetMrcDataLength(VOID)
{
  FSPM_UPD *FspmUpd;
  FSP_STACK_DATA *StackData;

  //
  // Get the UPD pointer
  //
  StackData = GetStackData();
  FspmUpd = StackData->Upd;
  ASSERT (FspmUpd != NULL);

  //
  // Return the requested value
  //
  return FspmUpd->FspmConfig.MrcDataLength;
}

UINT32 GetMrcDataPtr(VOID)
{
  FSPM_UPD *FspmUpd;
  FSP_STACK_DATA *StackData;

  //
  // Get the UPD pointer
  //
  StackData = GetStackData();
  FspmUpd = StackData->Upd;
  ASSERT (FspmUpd != NULL);

  //
  // Return the requested value
  //
  return FspmUpd->FspmConfig.MrcDataPtr;
}

UINT32 GetRmuBaseAddress(VOID)
{
  FSPM_UPD *FspmUpd;
  FSP_STACK_DATA *StackData;

  //
  // Get the UPD pointer
  //
  StackData = GetStackData();
  FspmUpd = StackData->Upd;
  ASSERT (FspmUpd != NULL);

  //
  // Return the requested value
  //
  return FspmUpd->FspmConfig.RmuBaseAddress;
}

UINT32 GetRmuLength(VOID)
{
  FSPM_UPD *FspmUpd;
  FSP_STACK_DATA *StackData;

  //
  // Get the UPD pointer
  //
  StackData = GetStackData();
  FspmUpd = StackData->Upd;
  ASSERT (FspmUpd != NULL);

  //
  // Return the requested value
  //
  return FspmUpd->FspmConfig.RmuLength;
}

UINT8 GetSmmTsegSize(VOID)
{
  FSPM_UPD *FspmUpd;
  FSP_STACK_DATA *StackData;

  //
  // Get the UPD pointer
  //
  StackData = GetStackData();
  FspmUpd = StackData->Upd;
  ASSERT (FspmUpd != NULL);

  //
  // Return the requested value
  //
  return FspmUpd->FspmConfig.SmmTsegSize;
}

FSP_STACK_DATA *GetStackData(VOID)
{
  FSP_STACK_DATA *StackData;

  StackData = (FSP_STACK_DATA *)QNCPortRead(
    QUARK_NC_MEMORY_CONTROLLER_SB_PORT_ID, SSKPD0);
  ASSERT(StackData != NULL);
  return StackData;
}

VOID *HobAllocate(UINT32 HobBytes)
{
  VOID *Hob;
  FSP_STACK_DATA *StackData;

  StackData = GetStackData();
  Hob = (VOID *)((UINT8 *)StackData->HobList - HobBytes);
  ASSERT(StackData->HeapStart != NULL);
  if ((VOID *)Hob < StackData->HeapStart)
    return NULL;

  //
  // Allocate and initialize the HOB
  //
  StackData->HobList = (VOID *)Hob;
  return Hob;
}

VOID InitializeHeap(UINTN HeapBaseAddress, UINTN HeapBytes)
{
  FSP_STACK_DATA *StackData;

  StackData = GetStackData();
  if (StackData->HeapStart != NULL)
    return;

  //
  // Initialize the heap
  //
  StackData->HeapStart = (VOID *)HeapBaseAddress;
  StackData->HobList = (VOID *)((UINT8 *)StackData->HeapStart + HeapBytes);
StackData->HobList = (VOID *)((UINT8 *)StackData->HeapStart + 0x400);
DEBUG((EFI_D_ERROR, "0x%08x: StackData.HobList\n", StackData->HobList));

  //
  // Create the end-of-list HOB
  //
  InternalPeiCreateHob(EFI_HOB_TYPE_END_OF_HOB_LIST,
                       sizeof(EFI_HOB_GENERIC_HEADER));
}
VOID ReturnHobListPointer(VOID *HobList)
{
  FSP_STACK_DATA *StackData;

  /* Return the address of the HOB list */
  StackData = GetStackData();
  if (StackData->HobListPtr != NULL) {
    *StackData->HobListPtr = HobList;
  }
}

VOID SaveStackData(FSP_STACK_DATA *StackData)
{
  /* Use a scratch pad register to hold the pointer */
  QNCPortWrite(QUARK_NC_MEMORY_CONTROLLER_SB_PORT_ID, SSKPD0,
    (UINT32)StackData);
}
