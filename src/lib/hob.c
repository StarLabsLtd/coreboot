/* SPDX-License-Identifier: GPL-2.0-only */

#include <arch/hlt.h>
#include <console/console.h>
#include <string.h>
#include <hob.h>

VOID      *mHobList;

/**
  Returns the pointer to the HOB list.
**/
VOID *
EFIAPI
GetHobList (
  VOID
  )
{
  if (!mHobList)
	  die("Hoblist is NULL\n");
  return mHobList;
}

EFI_HOB_HANDOFF_INFO_TABLE*
EFIAPI
HobTableInit (
  IN VOID   *EfiMemoryBegin,
  IN UINTN  EfiMemoryLength,
  IN VOID   *EfiFreeMemoryBottom,
  IN VOID   *EfiFreeMemoryTop
  )
{
  EFI_HOB_HANDOFF_INFO_TABLE  *Hob;
  EFI_HOB_GENERIC_HEADER      *HobEnd;

  Hob    = EfiFreeMemoryBottom;
  HobEnd = (EFI_HOB_GENERIC_HEADER *)(Hob+1);

  Hob->Header.HobType      = EFI_HOB_TYPE_HANDOFF;
  Hob->Header.HobLength    = sizeof(EFI_HOB_HANDOFF_INFO_TABLE);
  Hob->Header.Reserved     = 0;

  HobEnd->HobType          = EFI_HOB_TYPE_END_OF_HOB_LIST;
  HobEnd->HobLength        = sizeof(EFI_HOB_GENERIC_HEADER);
  HobEnd->Reserved         = 0;

  Hob->Version             = EFI_HOB_HANDOFF_TABLE_VERSION;
  Hob->BootMode            = BOOT_WITH_FULL_CONFIGURATION;

  Hob->EfiMemoryTop        = (UINTN)EfiMemoryBegin + EfiMemoryLength;
  Hob->EfiMemoryBottom     = (UINTN)EfiMemoryBegin;
  Hob->EfiFreeMemoryTop    = (UINTN)EfiFreeMemoryTop;
  Hob->EfiFreeMemoryBottom = (EFI_PHYSICAL_ADDRESS)(UINTN)(HobEnd+1);
  Hob->EfiEndOfHobList     = (EFI_PHYSICAL_ADDRESS)(UINTN)HobEnd;

  mHobList = Hob;
  return Hob;
}

static VOID *
EFIAPI
CreateHob (
  IN  UINT16    HobType,
  IN  UINT16    HobLength
  )
{
  EFI_HOB_HANDOFF_INFO_TABLE  *HandOffHob;
  EFI_HOB_GENERIC_HEADER      *HobEnd;
  EFI_PHYSICAL_ADDRESS        FreeMemory;
  VOID                        *Hob;

  HandOffHob = GetHobList ();

  HobLength = (UINT16)((HobLength + 0x7) & (~0x7));

  FreeMemory = HandOffHob->EfiFreeMemoryTop - HandOffHob->EfiFreeMemoryBottom;

  if (FreeMemory < HobLength) {
      return NULL;
  }

  Hob = (VOID*) (UINTN) HandOffHob->EfiEndOfHobList;
  ((EFI_HOB_GENERIC_HEADER*) Hob)->HobType = HobType;
  ((EFI_HOB_GENERIC_HEADER*) Hob)->HobLength = HobLength;
  ((EFI_HOB_GENERIC_HEADER*) Hob)->Reserved = 0;

  HobEnd = (EFI_HOB_GENERIC_HEADER*) ((UINTN)Hob + HobLength);
  HandOffHob->EfiEndOfHobList = (EFI_PHYSICAL_ADDRESS) (UINTN) HobEnd;

  HobEnd->HobType   = EFI_HOB_TYPE_END_OF_HOB_LIST;
  HobEnd->HobLength = sizeof(EFI_HOB_GENERIC_HEADER);
  HobEnd->Reserved  = 0;
  HobEnd++;
  HandOffHob->EfiFreeMemoryBottom = (EFI_PHYSICAL_ADDRESS) (UINTN) HobEnd;

  return Hob;
}

/**
  Builds a HOB that describes a chunk of system memory.
**/
VOID
EFIAPI
BuildResourceDescriptorHob (
  IN EFI_RESOURCE_TYPE            ResourceType,
  IN EFI_RESOURCE_ATTRIBUTE_TYPE  ResourceAttribute,
  IN EFI_PHYSICAL_ADDRESS         PhysicalStart,
  IN UINT64                       NumberOfBytes
  )
{
  EFI_HOB_RESOURCE_DESCRIPTOR  *Hob;

  Hob = CreateHob (EFI_HOB_TYPE_RESOURCE_DESCRIPTOR, sizeof (EFI_HOB_RESOURCE_DESCRIPTOR));
  if (!Hob) die(" error in build resource hob\n");

  Hob->ResourceType      = ResourceType;
  Hob->ResourceAttribute = ResourceAttribute;
  Hob->PhysicalStart     = PhysicalStart;
  Hob->ResourceLength    = NumberOfBytes;
}



/**
  Returns the next instance of a HOB type from the starting HOB.
**/
VOID *
EFIAPI
GetNextHob (
  IN UINT16                 Type,
  IN CONST VOID             *HobStart
  )
{
  EFI_PEI_HOB_POINTERS  Hob;

  if (!HobStart) die("Get next hob error\n");

  Hob.Raw = (UINT8 *) HobStart;
  // Parse the HOB list until end of list or matching type is found.
  while (!END_OF_HOB_LIST (Hob)) {
    if (Hob.Header->HobType == Type) {
      return Hob.Raw;
    }
    Hob.Raw = GET_NEXT_HOB (Hob);
  }
  return NULL;
}



/**
  Returns the first instance of a HOB type among the whole HOB list.
**/
VOID *
EFIAPI
GetFirstHob (
  IN UINT16                 Type
  )
{
  VOID      *HobList;

  HobList = GetHobList ();
  return GetNextHob (Type, HobList);
}


/**
**/
VOID *
EFIAPI
GetNextGuidHob (
  IN CONST EFI_GUID         *Guid,
  IN CONST VOID             *HobStart
  ){
  EFI_PEI_HOB_POINTERS  GuidHob;

  GuidHob.Raw = (UINT8 *) HobStart;
  while ((GuidHob.Raw = GetNextHob (EFI_HOB_TYPE_GUID_EXTENSION, GuidHob.Raw)) != NULL) {
    if (CompareGuid (Guid, &GuidHob.Guid->Name)) {
      break;
    }
    GuidHob.Raw = GET_NEXT_HOB (GuidHob);
  }
  return GuidHob.Raw;
}


/**
  This function searches the first instance of a HOB among the whole HOB list.
  Such HOB should satisfy two conditions:
**/
VOID *
EFIAPI
GetFirstGuidHob (
  IN CONST EFI_GUID         *Guid
  )
{
  VOID      *HobList;

  HobList = GetHobList ();
  return GetNextGuidHob (Guid, HobList);
}



/**
  Builds a GUID HOB with a certain data length.
**/
VOID *
EFIAPI
BuildGuidHob (
  IN CONST EFI_GUID              *Guid,
  IN UINTN                       DataLength
  )
{
  EFI_HOB_GUID_TYPE *Hob;

  // Make sure that data length is not too long.
  if (DataLength > (0xffff - sizeof (EFI_HOB_GUID_TYPE)))
	  die ("data length error");

  Hob = CreateHob (EFI_HOB_TYPE_GUID_EXTENSION, (UINT16) (sizeof (EFI_HOB_GUID_TYPE) + DataLength));
  CopyGuid (&Hob->Name, Guid);
  return Hob + 1;
}


/**
  Copies a data buffer to a newly-built HOB.
**/
VOID *
EFIAPI
BuildGuidDataHob (
  IN CONST EFI_GUID              *Guid,
  IN VOID                        *Data,
  IN UINTN                       DataLength
  )
{
  VOID  *HobData;

  if (!Data || !DataLength )
	  die("data is null or data lentgth is 0\n");

  HobData = BuildGuidHob (Guid, DataLength);

  return memcpy (HobData, Data, DataLength);
}


/**
  Builds a Firmware Volume HOB.
**/
VOID
EFIAPI
BuildFvHob (
  IN EFI_PHYSICAL_ADDRESS        BaseAddress,
  IN UINT64                      Length
  )
{
  EFI_HOB_FIRMWARE_VOLUME  *Hob;

  Hob = CreateHob (EFI_HOB_TYPE_FV, sizeof (EFI_HOB_FIRMWARE_VOLUME));

  Hob->BaseAddress = BaseAddress;
  Hob->Length      = Length;
}


/**
  Builds a EFI_HOB_TYPE_FV2 HOB.
**/
VOID
EFIAPI
BuildFv2Hob (
  IN          EFI_PHYSICAL_ADDRESS        BaseAddress,
  IN          UINT64                      Length,
  IN CONST    EFI_GUID                    *FvName,
  IN CONST    EFI_GUID                    *FileName
  )
{
  EFI_HOB_FIRMWARE_VOLUME2  *Hob;

  Hob = CreateHob (EFI_HOB_TYPE_FV2, sizeof (EFI_HOB_FIRMWARE_VOLUME2));

  Hob->BaseAddress = BaseAddress;
  Hob->Length      = Length;
  CopyGuid (&Hob->FvName, FvName);
  CopyGuid (&Hob->FileName, FileName);
}

/**
  Builds a EFI_HOB_TYPE_FV3 HOB.
**/
VOID
EFIAPI
BuildFv3Hob (
  IN          EFI_PHYSICAL_ADDRESS        BaseAddress,
  IN          UINT64                      Length,
  IN          UINT32                      AuthenticationStatus,
  IN          BOOLEAN                     ExtractedFv,
  IN CONST    EFI_GUID                    *FvName, OPTIONAL
  IN CONST    EFI_GUID                    *FileName OPTIONAL
  )
{
  EFI_HOB_FIRMWARE_VOLUME3  *Hob;

  Hob = CreateHob (EFI_HOB_TYPE_FV3, sizeof (EFI_HOB_FIRMWARE_VOLUME3));

  Hob->BaseAddress          = BaseAddress;
  Hob->Length               = Length;
  Hob->AuthenticationStatus = AuthenticationStatus;
  Hob->ExtractedFv          = ExtractedFv;
  if (ExtractedFv) {
    CopyGuid (&Hob->FvName, FvName);
    CopyGuid (&Hob->FileName, FileName);
  }
}


/**
  Builds a HOB for the CPU.
**/
VOID
EFIAPI
BuildCpuHob (
  IN UINT8                       SizeOfMemorySpace,
  IN UINT8                       SizeOfIoSpace
  )
{
  EFI_HOB_CPU  *Hob;

  Hob = CreateHob (EFI_HOB_TYPE_CPU, sizeof (EFI_HOB_CPU));

  Hob->SizeOfMemorySpace = SizeOfMemorySpace;
  Hob->SizeOfIoSpace     = SizeOfIoSpace;

  // Zero the reserved space to match HOB spec
  memset (Hob->Reserved, 0, sizeof (Hob->Reserved));
}



/**
  Builds a HOB for the memory allocation.
**/
VOID
EFIAPI
BuildMemoryAllocationHob (
  IN EFI_PHYSICAL_ADDRESS        BaseAddress,
  IN UINT64                      Length,
  IN EFI_MEMORY_TYPE             MemoryType
  )
{
  EFI_HOB_MEMORY_ALLOCATION  *Hob;

  if ((BaseAddress & (EFI_PAGE_SIZE - 1)) || (Length & (EFI_PAGE_SIZE - 1)))
	 die ("alignment");

  Hob = CreateHob (EFI_HOB_TYPE_MEMORY_ALLOCATION, sizeof (EFI_HOB_MEMORY_ALLOCATION));

 // DEBUG ((EFI_D_ERROR, "   BuildMemoryAllocationHob BaseAddress= 0x%llx,Length = 0x%llx, MemoryType= 0x%x\n",  BaseAddress, Length, MemoryType));
 // DEBUG ((EFI_D_ERROR, "   Hob = 0x%p\n",  Hob));

  memset (&(Hob->AllocDescriptor.Name), 0, sizeof (EFI_GUID));
  Hob->AllocDescriptor.MemoryBaseAddress = BaseAddress;
  Hob->AllocDescriptor.MemoryLength      = Length;
  Hob->AllocDescriptor.MemoryType        = MemoryType;
  // Zero the reserved space to match HOB spec
  memset (Hob->AllocDescriptor.Reserved, 0, sizeof (Hob->AllocDescriptor.Reserved));
}
