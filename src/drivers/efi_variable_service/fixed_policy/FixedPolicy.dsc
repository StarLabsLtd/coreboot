# SPDX-License-Identifier: BSD-2-Clause-Patent
[Defines]
  PLATFORM_NAME = CorebootFixedMmService
  PLATFORM_GUID = 41E48E5B-9C8F-4E63-8852-C70912C99055
  PLATFORM_VERSION = 1.0
  DSC_SPECIFICATION = 0x0001001B
  OUTPUT_DIRECTORY = Build/CorebootFixedMmService
  SUPPORTED_ARCHITECTURES = IA32
  BUILD_TARGETS = DEBUG|RELEASE
  SKUID_IDENTIFIER = DEFAULT
  FLASH_DEFINITION = UefiPayloadPkg/CorebootFixedMmService.fdf

!include UefiPayloadPkg/CorebootFixedMmService.dsc

[Components]
  MdeModulePkg/Universal/Variable/RuntimeDxe/VariableStandaloneMm.inf {
    <LibraryClasses>
!if $(CAPSULE_SUPPORT) == TRUE
      NULL|UefiPayloadPkg/CorebootCapsuleMm/OwnerMm.inf
!endif
      NULL|MdeModulePkg/Library/VarCheckUefiLib/VarCheckUefiLib.inf
      NULL|MdeModulePkg/Library/VarCheckPolicyLib/VarCheckPolicyLibStandaloneMm.inf
      NULL|src/drivers/efi_variable_service/fixed_policy/FixedPolicy.inf
  }
