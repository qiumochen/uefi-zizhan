/** @file
  zizhan M3 A' Android handoff action.

  The UniXNU menu boots this EFI application as its default "Android" entry.
  It writes the BootShim magic, cleans the data cache and requests the PSCI
  SYSTEM_RESET2 warm reset through the runtime service. The matching BootShim
  reads the magic after the ABL reloads the boot partition and jumps to the
  preserved Android Image with the ABL FDT in x0.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include <Uefi.h>

#define ZIZHAN_ANDROID_MAGIC_ADDR  0xA7500000ULL
#define ZIZHAN_ANDROID_MAGIC_WORD  0x5241U
#define ZIZHAN_ANDROID_MAGIC_WORDS 128U

EFI_STATUS
EFIAPI
AndroidHandoffEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  volatile UINT16  *Magic;
  UINTN            Index;
  UINTN            Address;

  (VOID)ImageHandle;
  Magic = (volatile UINT16 *)(UINTN)ZIZHAN_ANDROID_MAGIC_ADDR;
  for (Index = 0; Index < ZIZHAN_ANDROID_MAGIC_WORDS; ++Index) {
    Magic[Index] = ZIZHAN_ANDROID_MAGIC_WORD;
  }
  Address = (UINTN)ZIZHAN_ANDROID_MAGIC_ADDR;
  for (Index = 0; Index < ZIZHAN_ANDROID_MAGIC_WORDS * sizeof (UINT16); Index += 64) {
    __asm__ volatile ("dc civac, %0" : : "r" (Address + Index) : "memory");
  }
  __asm__ volatile ("dsb sy" : : : "memory");
  SystemTable->RuntimeServices->ResetSystem (EfiResetWarm, EFI_SUCCESS, 0, NULL);
  return EFI_SUCCESS;
}
