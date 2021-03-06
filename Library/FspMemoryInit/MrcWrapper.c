/** @file
Memory reference code wrapper for the Quark Memory Controller.

Copyright (c) 2013 - 2016, Intel Corporation.

This program and the accompanying materials are licensed and made available
under the terms and conditions of the BSD License which accompanies this
distribution.  The full text of the license may be found at
http://opensource.org/licenses/bsd-license.php

THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

**/

#include "CommonHeader.h"
#include "MrcWrapper.h"
#include <Library/MrcLib.h>

#define RESERVED_CPU_S3_SAVE_OFFSET (RESERVED_ACPI_S3_RANGE_OFFSET - sizeof (SMM_S3_RESUME_STATE))

// Strap configuration register specifying DDR setup
#define QUARK_SCSS_REG_STPDDRCFG   0x00

// Macro counting array elements
#define COUNT(a)                 (sizeof(a)/sizeof(*a))


EFI_MEMORY_TYPE_INFORMATION mDefaultQncMemoryTypeInformation[] = {
  { EfiMaxMemoryType,       0 }
};

UINT64
GetFspReservedMemorySize (VOID)
{
  UINTN  Index;
  UINT64 MemorySize;
  UINT32 FspReservedMemoryLength;

  //
  // Start with minimum memory
  //
  FspReservedMemoryLength = GetFspReservedMemoryLength();
  MemorySize = FspReservedMemoryLength;

  //
  // Account for other memory areas
  //
  for (Index = 0; Index < sizeof(mDefaultQncMemoryTypeInformation) / sizeof (EFI_MEMORY_TYPE_INFORMATION); Index++) {
    MemorySize += mDefaultQncMemoryTypeInformation[Index].NumberOfPages * EFI_PAGE_SIZE;
  }
  return MemorySize;
}

/**
  Configure MRC from memory controller fuse settings.

  @param  MrcData      - MRC configuration data to be updated.
**/
VOID
MrcConfigureFromMcFuses (
  OUT MRC_PARAMS                          *MrcData
  )
{
  UINT32                            McFuseStat;

  McFuseStat = QNCPortRead (
                 QUARK_NC_MEMORY_CONTROLLER_SB_PORT_ID,
                 QUARK_NC_MEMORY_CONTROLLER_REG_DFUSESTAT
                 );

  DEBUG ((EFI_D_INFO, "MRC McFuseStat 0x%08x\n", McFuseStat));

  if ((McFuseStat & B_DFUSESTAT_ECC_DIS) != 0) {
    DEBUG ((EFI_D_INFO, "MRC Fuse : fus_dun_ecc_dis.\n"));
    MrcData->ecc_enables = 0;
  } else {
    MrcData->ecc_enables = 1;
  }
}

/**
  Get MRC parametes from the BIOS.

  @param  MrcData      - MRC configuration data to be updated.
**/
VOID
MrcConfigureFromInfoHob (
  OUT MRC_PARAMS  *MrcData
  )
{
  // Get the memory parameters passed into FSP
  GetMemoryParameters(MrcData);
  MrcData->scrambling_enables  = (MrcData->scrambling_enables & MRC_FLAG_SCRAMBLE_EN) != 0;

  DEBUG ((EFI_D_INFO, "MRC dram_width %d\n",  MrcData->dram_width));
  DEBUG ((EFI_D_INFO, "MRC rank_enables %d\n",MrcData->rank_enables));
  DEBUG ((EFI_D_INFO, "MRC ddr_speed %d\n",   MrcData->ddr_speed));
  DEBUG ((EFI_D_INFO, "MRC flags: %s\n",
    (MrcData->scrambling_enables) ? L"SCRAMBLE_EN" : L""
    ));

  DEBUG ((EFI_D_INFO, "MRC density=%d tCL=%d tRAS=%d tWTR=%d tRRD=%d tFAW=%d\n",
    MrcData->params.DENSITY,
    MrcData->params.tCL,
    MrcData->params.tRAS,
    MrcData->params.tWTR,
    MrcData->params.tRRD,
    MrcData->params.tFAW
    ));
}

/**

  Configure ECC scrub

  @param MrcData - MRC configuration

**/
VOID
EccScrubSetup(
  const MRC_PARAMS *MrcData
  )
{
  UINT32 BgnAdr = 0;
  UINT32 EndAdr = MrcData->mem_size;
  UINT32 BlkSize = GetEccScrubBlkSize() & SCRUB_CFG_BLOCKSIZE_MASK;
  UINT32 Interval = GetEccScrubInterval() & SCRUB_CFG_INTERVAL_MASK;

  if( MrcData->ecc_enables == 0 || MrcData->boot_mode == bmS3 || Interval == 0) {
    // No scrub configuration needed if ECC not enabled
    // On S3 resume reconfiguration is done as part of resume
    // script, see SNCS3Save.c ==> SaveRuntimeScriptTable()
    // Also if PCD disables scrub, then we do nothing.
    return;
  }

  QNCPortWrite (QUARK_NC_RMU_SB_PORT_ID, QUARK_NC_ECC_SCRUB_END_MEM_REG, EndAdr);
  QNCPortWrite (QUARK_NC_RMU_SB_PORT_ID, QUARK_NC_ECC_SCRUB_START_MEM_REG, BgnAdr);
  QNCPortWrite (QUARK_NC_RMU_SB_PORT_ID, QUARK_NC_ECC_SCRUB_NEXT_READ_REG, BgnAdr);
  QNCPortWrite (QUARK_NC_RMU_SB_PORT_ID, QUARK_NC_ECC_SCRUB_CONFIG_REG,
    Interval << SCRUB_CFG_INTERVAL_SHIFT |
    BlkSize << SCRUB_CFG_BLOCKSIZE_SHIFT);

  McD0PciCfg32 (QNC_ACCESS_PORT_MCR) = SCRUB_RESUME_MSG();
}

/**

  This function returns the memory ranges to be enabled, along with information
  describing how the range should be used.

  @param  TimingData    Detected DDR timing parameters for installed memory.
  @param  RowConfArray  Pointer to an array of EFI_DUAL_CHANNEL_DDR_ROW_CONFIG structures. The number
                        of items in the array must match MaxRows returned by the McGetRowInfo() function.
  @param  MemoryMap     Buffer to record details of the memory ranges tobe enabled.
  @param  NumRanges     On input, this contains the maximum number of memory ranges that can be described
                        in the MemoryMap buffer.
  @param  RmuMainMemoryAddress Address of RMU binary in main memory

  @return MemoryMap     The buffer will be filled in
          NumRanges     will contain the actual number of memory ranges that are to be anabled.
          EFI_SUCCESS   The function completed successfully.

**/
EFI_STATUS
GetMemoryMap (
  IN     UINT32                                TotalMemorySize,
  IN OUT PEI_DUAL_CHANNEL_DDR_MEMORY_MAP_RANGE *MemoryMap,
  IN OUT UINT8                                 *NumRanges,
  IN OUT UINT32                                *RmuMainMemoryAddress
  )
{
  EFI_PHYSICAL_ADDRESS              MemoryAddress;
  EFI_PHYSICAL_ADDRESS              MemorySize;
  UINT8                             ExtendedMemoryIndex;
  UINT32                            Register;
  UINT32                            TsegSize;

  if ((*NumRanges) < MAX_RANGES) {
    return EFI_BUFFER_TOO_SMALL;
  }

  *NumRanges = 0;

  //
  // Generate Memory ranges for the memory map.
  //

  //
  // Add memory below 640KB to the memory map. Make sure memory between
  // 640KB and 1MB are reserved, even if not used for SMRAM
  //
  MemoryMap[*NumRanges].PhysicalAddress = 0;
  MemoryMap[*NumRanges].CpuAddress      = 0;
  MemoryMap[*NumRanges].RangeLength     = 0xA0000;
  MemoryMap[*NumRanges].Type            = DualChannelDdrMainMemory;
  (*NumRanges)++;

  //
  // Just mark this range reserved
  //
  MemoryMap[*NumRanges].PhysicalAddress = 0xA0000;
  MemoryMap[*NumRanges].CpuAddress      = 0xA0000;
  MemoryMap[*NumRanges].RangeLength     = 0x60000;
  MemoryMap[*NumRanges].Type            = DualChannelDdrGraphicsReservedMemory;
  (*NumRanges)++;

  //
  // Add remaining memory to the memory map
  //
  MemoryAddress = 0x100000;
  MemorySize = TotalMemorySize - MemoryAddress;

  MemoryMap[*NumRanges].PhysicalAddress = MemoryAddress;
  MemoryMap[*NumRanges].CpuAddress      = MemoryAddress;
  MemoryMap[*NumRanges].RangeLength     = MemorySize;
  MemoryMap[*NumRanges].Type            = DualChannelDdrMainMemory;
  (*NumRanges)++;
  MemoryAddress += MemorySize;

  ExtendedMemoryIndex = (UINT8) (*NumRanges - 1);

  // ------------------------ Top of physical memory
  //
  //      --------------      TSEG + 1 page
  // S3 Memory base structure
  //      --------------      RESERVED_ACPI_S3_RANGE_OFFSET
  // CPU S3 data
  //      --------------      RESERVED_CPU_S3_SAVE_OFFSET
  //
  // ------------------------ TSEG Base
  // Copy of RMU binary
  // ------------------------ TOLUM: RmuBaseAddress
  // BIOS reserved area
  // ------------------------
  // FSP reserved area
  // ------------------------
  // DRAM
  // ------------------------ 0x00100000
  // DRAM
  // ------------------------ 0x000A0000
  // DRAM
  // ------------------------ 0

  //
  // See if we need to trim TSEG out of the highest memory range
  //
  TsegSize = GetSmmTsegSize();
  Register = (UINT32)((MemoryAddress - 1) & SMM_END_MASK);
  if (TsegSize > 0) {
    MemoryMap[*NumRanges].RangeLength           = (TsegSize * 1024 * 1024);
    MemoryAddress                              -= MemoryMap[*NumRanges].RangeLength;
    MemoryMap[*NumRanges].PhysicalAddress       = MemoryAddress;
    MemoryMap[*NumRanges].CpuAddress            = MemoryAddress;
    MemoryMap[ExtendedMemoryIndex].RangeLength -= MemoryMap[*NumRanges].RangeLength;
    MemoryMap[*NumRanges].Type = DualChannelDdrSmramCacheable;
    (*NumRanges)++;
  }

  //
  // Set the TSEG base address
  //
  Register |= (UINT32)(((RShiftU64(MemoryAddress, 16)) & SMM_START_MASK)
            | SMM_WRITE_OPEN | SMM_READ_OPEN | SMM_CODE_RD_OPEN);
  QncHsmmcWrite (Register);

  //
  // Trim off 64K memory for RMU Main binary shadow
  //
  MemoryMap[*NumRanges].RangeLength           = 0x10000;
  ASSERT(MemoryMap[*NumRanges].RangeLength >= GetRmuLength());
  MemoryAddress                              -= MemoryMap[*NumRanges].RangeLength;
  MemoryMap[*NumRanges].PhysicalAddress       = MemoryAddress;
  MemoryMap[*NumRanges].CpuAddress            = MemoryAddress;
  MemoryMap[ExtendedMemoryIndex].RangeLength -= MemoryMap[*NumRanges].RangeLength;
  MemoryMap[*NumRanges].Type = DualChannelDdrRmuReservedMemory;
  (*NumRanges)++;

  //
  // Return the RMU base address
  //
  *RmuMainMemoryAddress = (UINT32)MemoryAddress;

  //
  // Trim off the BIOS reserved area
  //
  if (GetBootLoaderTolumSize() > 0) {
    MemoryMap[*NumRanges].RangeLength           = GetBootLoaderTolumSize();
    MemoryAddress                              -= MemoryMap[*NumRanges].RangeLength;
    MemoryMap[*NumRanges].PhysicalAddress       = MemoryAddress;
    MemoryMap[*NumRanges].CpuAddress            = MemoryAddress;
    MemoryMap[ExtendedMemoryIndex].RangeLength -= MemoryMap[*NumRanges].RangeLength;
    MemoryMap[*NumRanges].Type = DualChannelDdrBiosReservedMemory;
    (*NumRanges)++;
  }

  //
  // Trim off the FSP reserved area
  //
  MemoryMap[*NumRanges].RangeLength           = GetFspReservedMemorySize();
  MemoryAddress                              -= MemoryMap[*NumRanges].RangeLength;
  MemoryMap[*NumRanges].PhysicalAddress       = MemoryAddress;
  MemoryMap[*NumRanges].CpuAddress            = MemoryAddress;
  MemoryMap[ExtendedMemoryIndex].RangeLength -= MemoryMap[*NumRanges].RangeLength;
  MemoryMap[*NumRanges].Type = DualChannelDdrFspReservedMemory;
  (*NumRanges)++;

  //
  // Display the memory segments
  //
  DEBUG_CODE_BEGIN();
  {
    UINT32 Index;

    MemoryAddress = TotalMemorySize;
    DEBUG ((EFI_D_ERROR, "+------------------------- 0x%08x: Top of physical memory\n", MemoryAddress));
    Index = *NumRanges - 1;

    // Display SMM area if enabled
    Index = ExtendedMemoryIndex + 1;
    if (TsegSize > 0) {
      MemoryAddress -= MemoryMap[Index].RangeLength;
      ASSERT(MemoryAddress == MemoryMap[Index].PhysicalAddress);
      DEBUG ((EFI_D_ERROR, "|\n"));
      DEBUG ((EFI_D_ERROR, "|      --------------      0x%08x: TSEG + 1 page\n", MemoryAddress + EFI_PAGE_SIZE));
      DEBUG ((EFI_D_ERROR, "| S3 Memory base structure\n"));
      DEBUG ((EFI_D_ERROR, "|      --------------      0x%08x: RESERVED_ACPI_S3_RANGE_OFFSET\n", MemoryAddress + RESERVED_ACPI_S3_RANGE_OFFSET));
      DEBUG ((EFI_D_ERROR, "| CPU S3 data\n"));
      DEBUG ((EFI_D_ERROR, "|      --------------      0x%08x: RESERVED_CPU_S3_SAVE_OFFSET\n", MemoryAddress + RESERVED_CPU_S3_SAVE_OFFSET));
      DEBUG ((EFI_D_ERROR, "|\n"));
      DEBUG ((EFI_D_ERROR, "+------------------------- 0x%08x: TSEG Base\n", MemoryAddress));
      Index++;
    }

    // Display RMU area
    MemoryAddress -= MemoryMap[Index].RangeLength;
    ASSERT(MemoryAddress == MemoryMap[Index].PhysicalAddress);
    DEBUG ((EFI_D_ERROR, "| Copy of RMU binary\n"));
    DEBUG ((EFI_D_ERROR, "+------------------------- 0x%08x: RmuBaseAddress (TOLUM)\n", MemoryAddress));
    Index++;

    // Display BIOS reserved area if requested
    if (GetBootLoaderTolumSize() > 0) {
      MemoryAddress -= MemoryMap[Index].RangeLength;
      ASSERT(MemoryAddress == MemoryMap[Index].PhysicalAddress);
      DEBUG ((EFI_D_ERROR, "| BIOS reserved area\n"));
      DEBUG ((EFI_D_ERROR, "+------------------------- 0x%08x\n", MemoryAddress));
      Index++;
    }

    // Display FSP reserved area
    MemoryAddress -= MemoryMap[Index].RangeLength;
    ASSERT(MemoryAddress == MemoryMap[Index].PhysicalAddress);
    DEBUG ((EFI_D_ERROR, "| FSP reserved area\n"));
    DEBUG ((EFI_D_ERROR, "+------------------------- 0x%08x\n", MemoryAddress));

    // Display DRAM areas
    Index = ExtendedMemoryIndex;
    do {
      MemoryAddress -= MemoryMap[Index].RangeLength;
      ASSERT(MemoryAddress == MemoryMap[Index].PhysicalAddress);
      DEBUG ((EFI_D_ERROR, "| DRAM\n"));
      DEBUG ((EFI_D_ERROR, "+------------------------- 0x%08x\n", MemoryAddress));
    } while (Index-- != 0);
  }
  DEBUG_CODE_END ();
  return EFI_SUCCESS;
}

EFI_STATUS
BaseMemoryTest (
  IN  EFI_PHYSICAL_ADDRESS               BeginAddress,
  IN  UINT64                             MemoryLength,
  IN  PEI_MEMORY_TEST_OP                 Operation,
  OUT EFI_PHYSICAL_ADDRESS               *ErrorAddress
  )
{
  UINT32                TestPattern;
  EFI_PHYSICAL_ADDRESS  TempAddress;
  UINT32                SpanSize;

  TestPattern = 0x5A5A5A5A;
  SpanSize    = 0;

  //
  // Make sure we don't try and test anything above the max physical address range
  //
  ASSERT (BeginAddress + MemoryLength < MAX_ADDRESS);

  switch (Operation) {
  case Extensive:
    SpanSize = 0x4;
    break;

  case Sparse:
  case Quick:
    SpanSize = 0x40000;
    break;

  case Ignore:
    goto Done;
    break;
  }
  //
  // Write the test pattern into memory range
  //
  TempAddress = BeginAddress;
  while (TempAddress < BeginAddress + MemoryLength) {
    (*(UINT32 *) (UINTN) TempAddress) = TestPattern;
    TempAddress += SpanSize;
  }
  //
  // Read pattern from memory and compare it
  //
  TempAddress = BeginAddress;
  while (TempAddress < BeginAddress + MemoryLength) {
    if ((*(UINT32 *) (UINTN) TempAddress) != TestPattern) {
      *ErrorAddress = TempAddress;
      DEBUG ((EFI_D_ERROR, "Memory test failed at 0x%x.\n", TempAddress));
      return EFI_DEVICE_ERROR;
    }

    TempAddress += SpanSize;
  }

Done:
  return EFI_SUCCESS;
}

/**

  This function installs memory.

  @param   MemoryMap       Array of memory ranges in the system
  @param   NumRanges       Number of entries in the MemoryMap array
  @param   FspReservedArea Address of the FSP reserved area
  @param   ReservedBytes   Size of the FSP reserved area
  @param   MrcData         Memory data to save for S3

**/
VOID
BuildHobs (
  IN      PEI_DUAL_CHANNEL_DDR_MEMORY_MAP_RANGE      *MemoryMap,
  IN      UINT8                                      NumRanges,
  IN      EFI_PHYSICAL_ADDRESS                       FspReservedArea,
  IN      UINT64                                     ReservedBytes,
  IN      MRC_PARAMS                                 *MrcData
  )
{
  UINT8                                 Index;
  EFI_RESOURCE_ATTRIBUTE_TYPE           Attribute;

  //
  // Build the descriptor for the FSP reserved area
  //
  BuildResourceDescriptorHob (
   EFI_RESOURCE_SYSTEM_MEMORY,                       // MemoryType,
   (
   EFI_RESOURCE_ATTRIBUTE_PRESENT |
   EFI_RESOURCE_ATTRIBUTE_INITIALIZED |
   EFI_RESOURCE_ATTRIBUTE_TESTED |
   EFI_RESOURCE_ATTRIBUTE_UNCACHEABLE |
   EFI_RESOURCE_ATTRIBUTE_WRITE_COMBINEABLE |
   EFI_RESOURCE_ATTRIBUTE_WRITE_THROUGH_CACHEABLE |
   EFI_RESOURCE_ATTRIBUTE_WRITE_BACK_CACHEABLE
   ),
   FspReservedArea,                                  // MemoryBegin
   ReservedBytes                                     // MemoryLength
   );

  //
  // Install physical memory descriptor hobs for each memory range.
  //
  for (Index = 0; Index < NumRanges; Index++) {
    Attribute = 0;
    if (MemoryMap[Index].Type == DualChannelDdrMainMemory)
    {
      //
      // This is Main Memory range, give it to EFI
      //
      DEBUG((EFI_D_INFO, "0x%08lx - 0x%08lx: %atested memory\n",
        MemoryMap[Index].PhysicalAddress,
        MemoryMap[Index].PhysicalAddress + MemoryMap[Index].RangeLength,
        (Index == 0) ? "Partially " : "Un"));
      BuildResourceDescriptorHob (
        EFI_RESOURCE_SYSTEM_MEMORY,       // MemoryType,
        (
        EFI_RESOURCE_ATTRIBUTE_PRESENT |
        EFI_RESOURCE_ATTRIBUTE_INITIALIZED |
        EFI_RESOURCE_ATTRIBUTE_UNCACHEABLE |
        EFI_RESOURCE_ATTRIBUTE_WRITE_COMBINEABLE |
        EFI_RESOURCE_ATTRIBUTE_WRITE_THROUGH_CACHEABLE |
        EFI_RESOURCE_ATTRIBUTE_WRITE_BACK_CACHEABLE
        ),
        MemoryMap[Index].PhysicalAddress, // MemoryBegin
        MemoryMap[Index].RangeLength      // MemoryLength
        );
    } else {
      if ((MemoryMap[Index].Type == DualChannelDdrSmramCacheable) ||
          (MemoryMap[Index].Type == DualChannelDdrSmramNonCacheable)) {
        DEBUG((EFI_D_ERROR, "0x%08lx - 0x%08lx: SMM\n",
          MemoryMap[Index].PhysicalAddress,
          MemoryMap[Index].PhysicalAddress + MemoryMap[Index].RangeLength));
      }
      if ((MemoryMap[Index].Type == DualChannelDdrSmramNonCacheable) ||
          (MemoryMap[Index].Type == DualChannelDdrGraphicsMemoryNonCacheable)) {
        DEBUG((EFI_D_ERROR, "0x%08lx - 0x%08lx: uncached\n",
          MemoryMap[Index].PhysicalAddress,
          MemoryMap[Index].PhysicalAddress + MemoryMap[Index].RangeLength));
        Attribute |= EFI_RESOURCE_ATTRIBUTE_UNCACHEABLE;
      }
      if ((MemoryMap[Index].Type == DualChannelDdrSmramCacheable)         ||
          (MemoryMap[Index].Type == DualChannelDdrGraphicsMemoryCacheable)) {
        //
        // TSEG and HSEG can be used with a write-back(WB) cache policy; however,
        // the specification requires that the TSEG and HSEG space be cached only
        // inside of the SMI handler. when using HSEG or TSEG an IA-32 processor
        // does not automatically write back and invalidate its cache before entering
        // SMM or before existing SMM therefore any MTRR defined for the active TSEG
        // or HSEG must be set to un-cacheable(UC) outside of SMM.
        //
        DEBUG((EFI_D_ERROR, "0x%08lx - 0x%08lx: Reserved, cached with write-back\n",
          MemoryMap[Index].PhysicalAddress,
          MemoryMap[Index].PhysicalAddress + MemoryMap[Index].RangeLength));
        Attribute |= EFI_RESOURCE_ATTRIBUTE_WRITE_BACK_CACHEABLE | EFI_RESOURCE_ATTRIBUTE_UNCACHEABLE;
      }
      if ((MemoryMap[Index].Type == DualChannelDdrBiosReservedMemory)
        || (MemoryMap[Index].Type == DualChannelDdrFspReservedMemory)
        || (MemoryMap[Index].Type == DualChannelDdrGraphicsReservedMemory)
        || (MemoryMap[Index].Type == DualChannelDdrRmuReservedMemory)) {
        DEBUG((EFI_D_ERROR, "0x%08lx - 0x%08lx: Reserved, cached with write-back\n",
          MemoryMap[Index].PhysicalAddress,
          MemoryMap[Index].PhysicalAddress + MemoryMap[Index].RangeLength));
        Attribute |= EFI_RESOURCE_ATTRIBUTE_WRITE_BACK_CACHEABLE |
                     EFI_RESOURCE_ATTRIBUTE_UNCACHEABLE;
      }

      //
      // Make sure non-system memory is marked as reserved
      //
      BuildResourceDescriptorHob (
        EFI_RESOURCE_MEMORY_RESERVED,     // MemoryType,
        Attribute,                        // MemoryAttribute
        MemoryMap[Index].PhysicalAddress, // MemoryBegin
        MemoryMap[Index].RangeLength      // MemoryLength
        );
    }
  }

  //
  // FSP BOOTLOADER TOLUM HOB
  //
  for (Index = 0; Index < NumRanges; Index++) {
    if (MemoryMap[Index].Type == DualChannelDdrBiosReservedMemory) {
      BuildResourceDescriptorWithOwnerHob (
        EFI_RESOURCE_MEMORY_RESERVED,            // MemoryType,
        (
          EFI_RESOURCE_ATTRIBUTE_PRESENT |
          EFI_RESOURCE_ATTRIBUTE_INITIALIZED |
          EFI_RESOURCE_ATTRIBUTE_TESTED |
          EFI_RESOURCE_ATTRIBUTE_UNCACHEABLE |
          EFI_RESOURCE_ATTRIBUTE_WRITE_COMBINEABLE |
          EFI_RESOURCE_ATTRIBUTE_WRITE_THROUGH_CACHEABLE |
          EFI_RESOURCE_ATTRIBUTE_WRITE_BACK_CACHEABLE
        ),
        MemoryMap[Index].PhysicalAddress,
        MemoryMap[Index].RangeLength,
        &gFspBootLoaderTolumHobGuid
        );
    }
  }

  //
  // FSP Reserved Memory
  //
  for (Index = 0; Index < NumRanges; Index++) {
    if (MemoryMap[Index].Type == DualChannelDdrFspReservedMemory) {
      BuildResourceDescriptorWithOwnerHob(
        EFI_RESOURCE_MEMORY_RESERVED,
        (
          EFI_RESOURCE_ATTRIBUTE_PRESENT |
          EFI_RESOURCE_ATTRIBUTE_INITIALIZED |
          EFI_RESOURCE_ATTRIBUTE_TESTED |
          EFI_RESOURCE_ATTRIBUTE_UNCACHEABLE |
          EFI_RESOURCE_ATTRIBUTE_WRITE_COMBINEABLE |
          EFI_RESOURCE_ATTRIBUTE_WRITE_THROUGH_CACHEABLE |
          EFI_RESOURCE_ATTRIBUTE_WRITE_BACK_CACHEABLE
        ),
        MemoryMap[Index].PhysicalAddress,
        MemoryMap[Index].RangeLength,
        &gFspReservedMemoryResourceHobGuid
      );
    }
  }

  //
  // Save the MRC data for S3
  //
  DEBUG ((EFI_D_INFO, "SaveConfig.\n"));
  BuildGuidDataHob (
    &gFspNonVolatileStorageHobGuid,
    (VOID *) &MrcData->timings,
    ((sizeof (MrcData->timings) + 0x7) & (~0x7))
    );
}

/** Post InstallS3Memory / InstallEfiMemory tasks given MrcData context.

  @param[in]       MrcData              MRC configuration.
  @param[in]       IsS3                 TRUE if after InstallS3Memory.
  @param[in]       RmuMainMemoryAddress Address of RMU binary in main memory

**/
VOID
PostInstallMemory (
  IN MRC_PARAMS                           *MrcData,
  IN BOOLEAN                              IsS3,
  IN UINT32                               RmuMainMemoryAddress
  )
{
  //
  // Setup ECC policy (All boot modes).
  //
  QNCPolicyDblEccBitErr (V_WDT_CONTROL_DBL_ECC_BIT_ERR_WARM);

  //
  // Relocate RMU binary into main memory
  //
  if (!IsS3) {
    DEBUG((EFI_D_ERROR, "RmuMainMemoryAddress: 0x%08x\n", RmuMainMemoryAddress));
    CopyMem ((VOID *)(UINTN)RmuMainMemoryAddress,
      (VOID *)(UINTN)GetRmuBaseAddress(), GetRmuLength());
  }

  //
  // Notify hardware of new RMU binary address
  //
  QNCSendOpcodeDramReady (RmuMainMemoryAddress);

  //
  // Scrub the ECC if necessary
  //
  if (!IsS3) {
    EccScrubSetup (MrcData);
  }
}

/**

  Do memory initialisation for QNC DDR3 SDRAM Controller

  @return EFI_SUCCESS  Memory initialisation completed successfully.
          All other error conditions encountered result in an ASSERT.

**/
EFI_STATUS
MemoryInit (
  VOID
  )
{
  MRC_PARAMS                                 MrcData;
  EFI_BOOT_MODE                               BootMode;
  EFI_STATUS                                  Status;
  EFI_STATUS_CODE_VALUE                       ErrorCodeValue;
  UINT16                                      PmswAdr;
  PEI_DUAL_CHANNEL_DDR_MEMORY_MAP_RANGE MemoryMap[MAX_RANGES];
  UINT8                                 NumRanges;
  EFI_PHYSICAL_ADDRESS                  BadMemoryAddress;
  EFI_PHYSICAL_ADDRESS                  FspReservedArea;
  UINT64                                ReservedBytes;
  UINT32                                RmuMainMemoryAddress;

  ErrorCodeValue  = 0;

  //
  // It is critical that both of these data structures are initialized to 0.
  // This PEIM knows the number of DIMMs in the system and works with that
  // information.  The MCH PEIM that consumes these data structures does not
  // know the number of DIMMs so it expects the entire structure to be
  // properly initialized.  By initializing these to zero, all flags indicating
  // that the SPD is present or the row should be configured are set to false.
  //
  ZeroMem (&MrcData, sizeof(MrcData));

  //
  // Determine boot mode
  //
  BootMode = GetBootMode();

  //
  // Initialize Error type for reporting status code
  //
  switch (BootMode) {
  case BOOT_ON_FLASH_UPDATE:
    ErrorCodeValue = EFI_COMPUTING_UNIT_MEMORY + EFI_CU_MEMORY_EC_UPDATE_FAIL;
    break;
  case BOOT_ON_S3_RESUME:
    ErrorCodeValue = EFI_COMPUTING_UNIT_MEMORY + EFI_CU_MEMORY_EC_S3_RESUME_FAIL;
    break;
  default:
    ErrorCodeValue = EFI_COMPUTING_UNIT_MEMORY;
    break;
  }

  //
  // Specify MRC boot mode
  //
  switch (BootMode) {
  case BOOT_ON_S3_RESUME:
  case BOOT_ON_FLASH_UPDATE:
    MrcData.boot_mode = bmS3;
    break;
  case BOOT_ASSUMING_NO_CONFIGURATION_CHANGES:
    MrcData.boot_mode = bmFast;
    break;
  default:
    MrcData.boot_mode = bmCold;
    break;
  }

  //
  // Configure MRC input parameters.
  //
  MrcConfigureFromMcFuses (&MrcData);
  MrcConfigureFromInfoHob (&MrcData);

  if (BootMode == BOOT_IN_RECOVERY_MODE) {
    //
    // Always do bmCold on recovery.
    //
    DEBUG ((DEBUG_INFO, "MemoryInit:Force bmCold on Recovery\n"));
    MrcData.boot_mode = bmCold;
  } else {

    //
    // Get the saved memory data if possible
    //
    if ((GetMrcDataPtr() != 0) && (GetMrcDataLength() != 0)) {
      ASSERT(GetMrcDataLength() == sizeof(MrcData.timings));
      CopyMem (&MrcData.timings, (void *)GetMrcDataPtr(), GetMrcDataLength());
    } else {
      switch (BootMode) {
      case BOOT_ON_S3_RESUME:
      case BOOT_ON_FLASH_UPDATE:
        DEBUG ((DEBUG_ERROR, "ERROR: MRC data missing - reboot\n"));
        REPORT_STATUS_CODE (
          EFI_ERROR_CODE + EFI_ERROR_UNRECOVERED,
          ErrorCodeValue
        );
        return FSP_STATUS_RESET_REQUIRED_COLD;
        break;

      default:
        MrcData.boot_mode = bmCold;
        break;
      }
    }
  }

  PmswAdr = (UINT16)(LpcPciCfg32 (R_QNC_LPC_GPE0BLK) & 0xFFFF) + R_QNC_GPE0BLK_PMSW;
  if( IoRead32 (PmswAdr) & B_QNC_GPE0BLK_PMSW_DRAM_INIT) {
    // MRC did not complete last execution, force cold boot path
    MrcData.boot_mode = bmCold;
  }

  // Mark MRC pending
  IoOr32 (PmswAdr, (UINT32)B_QNC_GPE0BLK_PMSW_DRAM_INIT);

  //
  // Call Memory Reference Code's Routines
  //
  Mrc (&MrcData);

  // Mark MRC completed
  IoAnd32 (PmswAdr, ~(UINT32)B_QNC_GPE0BLK_PMSW_DRAM_INIT);

  //
  // Get the Memory Map
  //
  NumRanges = MAX_RANGES;
  ZeroMem (MemoryMap, sizeof (PEI_DUAL_CHANNEL_DDR_MEMORY_MAP_RANGE) * NumRanges);
  Status = GetMemoryMap (
             MrcData.mem_size,
             (PEI_DUAL_CHANNEL_DDR_MEMORY_MAP_RANGE *) MemoryMap,
             &NumRanges,
             &RmuMainMemoryAddress
             );
  ASSERT_EFI_ERROR (Status);
  ASSERT(NumRanges <= MAX_RANGES);

  //
  // Locate the FSP reserved memory (last entry).
  //
  FspReservedArea = MemoryMap[NumRanges - 1].PhysicalAddress;
  ReservedBytes = MemoryMap[NumRanges - 1].RangeLength;

  //
  // Test the memory from 1M->TOM
  //
  if (BootMode != BOOT_ON_S3_RESUME) {
    if (BootMode != BOOT_ON_FLASH_UPDATE) {
      Status = BaseMemoryTest (
                0x100000,
                (MrcData.mem_size - 0x100000),
                Quick,
                &BadMemoryAddress
                );
      ASSERT_EFI_ERROR (Status);
    }

    //
    // Assign physical memory to PEI
    //
    FspInstallPeiMemory (FspReservedArea, ReservedBytes);
  }

  //
  // Enable memory for use
  //
  PostInstallMemory (&MrcData, FALSE, RmuMainMemoryAddress);

  //
  // Save the memory configuration data into a HOB
  // HOB data size (stored in variable) is required to be multiple of 8 bytes
  //
  if (BootMode != BOOT_ON_S3_RESUME) {
    InitializeHeap((UINTN)FspReservedArea, (UINTN)ReservedBytes);
    BuildHobs (MemoryMap,
               NumRanges,
               FspReservedArea,
               ReservedBytes,
               &MrcData);
  }

  DEBUG ((EFI_D_INFO, "MemoryInit Complete.\n"));
  return EFI_SUCCESS;
}
