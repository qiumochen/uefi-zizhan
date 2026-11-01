##
#  Copyright (c) 2011 - 2022, ARM Limited. All rights reserved.
#  Copyright (c) 2014, Linaro Limited. All rights reserved.
#  Copyright (c) 2015 - 2020, Intel Corporation. All rights reserved.
#  Copyright (c) 2018, Bingxing Wang. All rights reserved.
#  Copyright (c) Microsoft Corporation.
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
##

################################################################################
#
# Defines Section - statements that will be processed to create a Makefile.
#
################################################################################
[Defines]
  PLATFORM_NAME                  = zizhan
  PLATFORM_GUID                  = 7A1F2C3D-5E6B-4C2A-9D41-2F6E8A3B1C50
  PLATFORM_VERSION               = 0.1
  DSC_SPECIFICATION              = 0x00010005
  OUTPUT_DIRECTORY               = Build/zizhanPkg
  SUPPORTED_ARCHITECTURES        = AARCH64
  BUILD_TARGETS                  = RELEASE|DEBUG
  SKUID_IDENTIFIER               = DEFAULT
  FLASH_DEFINITION               = zizhanPkg/zizhan.fdf
  USE_CUSTOM_DISPLAY_DRIVER      = 0

  #
  # 0 = SM8475
  # 1 = SM8475    (Overclocked)
  # 2 = SM7475-AB
  #
  SOC_TYPE                       = 0

!include PalimaPkg/PalimaPkg.dsc.inc

[PcdsFixedAtBuild]
  #
  # DDR Memory
  #
  gArmTokenSpaceGuid.PcdSystemMemoryBase|0x80000000

  #
  # UEFI Stack
  #
  gArmPlatformTokenSpaceGuid.PcdCPUCoresStackBase|0xA760D000
  gArmPlatformTokenSpaceGuid.PcdCPUCorePrimaryStackSize|0x40000

  #
  # SMBIOS
  #
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemManufacturer|"Xiaomi"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemModel|"MIX Fold 2"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemRetailModel|"zizhan"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemRetailSku|"MIX_Fold_2_zizhan"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemBoardModel|"MIX Fold 2"

  #
  # Simple Frame Buffer
  #
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferWidth|2160
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferHeight|1916
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferColorDepth|32

  #
  # Platform PEI
  #
  gQcomPkgTokenSpaceGuid.PcdPlatformType|"LA"

[LibraryClasses]
  # UniXNU boot manager core; DarwinBoot provides the XNU loader protocol.
  UniXNULib|UniXNUPkg/Library/UniXNULib/UniXNULib.inf

  #
  # Memory Libraries
  #
  MemoryMapLib|zizhanPkg/Library/MemoryMapLib/MemoryMapLib.inf

  #
  # QCOM Libraries
  #
  ConfigurationMapLib|zizhanPkg/Library/ConfigurationMapLib/ConfigurationMapLib.inf

[Components]
  UniXNUPkg/Application/UniXNU/UniXNU.inf
  UniXNUPkg/Driver/UniXNUDxe/UniXNUDxe.inf
  DarwinBootPkg/Driver/DarwinBoot.inf
  zizhanPkg/Drivers/PongBootMemoryDxe/PongBootMemoryDxe.inf
  zizhanPkg/Test/PongPayloadProbe/PongPayloadProbe.inf
  zizhanPkg/Test/PongInitrdTest/PongInitrdTest.inf
  zizhanPkg/Applications/RamoopsView/RamoopsView.inf
  zizhanPkg/Applications/RamoopsDump/RamoopsDump.inf

  #
  # ACPI Tables
  #
  pong/AcpiTables.inf
