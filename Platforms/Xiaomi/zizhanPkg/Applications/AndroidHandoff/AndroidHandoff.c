/** @file
  zizhan M3 A' Android handoff action.

  The UniXNU menu boots this EFI application as its default "Android" entry.
  It writes the BootShim magic, cleans the data cache and requests the PSCI
  SYSTEM_RESET2 warm reset directly (SMC), then through the runtime service,
  and finally arms a 1 s Gunyah watchdog bite so it never returns to the menu. The matching BootShim
  reads the magic after the ABL reloads the boot partition and jumps to the
  preserved Android Image with the ABL FDT in x0.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include <Uefi.h>

#define ZIZHAN_ANDROID_MAGIC_ADDR  0xA7500000ULL
#define ZIZHAN_ANDROID_MAGIC_WORD  0x5241U
#define ZIZHAN_ANDROID_MAGIC_WORDS 128U

#define ZIZHAN_PSCI_FEATURES        0x8400000AULL
#define ZIZHAN_PSCI_SYSTEM_RESET2   0xC4000012ULL
#define ZIZHAN_GUNYAH_WDT_CONTROL   0x86000005ULL
#define ZIZHAN_GUNYAH_WDT_SET_TIME  0x86000008ULL
#define ZIZHAN_GUNYAH_WDT_ENABLE    3ULL
#define ZIZHAN_GUNYAH_WDT_DISABLE   2ULL

STATIC
UINT64
ZizhanSmc (
  IN UINT64  Function,
  IN UINT64  Arg1,
  IN UINT64  Arg2
  )
{
  register UINT64  X0 __asm__ ("x0") = Function;
  register UINT64  X1 __asm__ ("x1") = Arg1;
  register UINT64  X2 __asm__ ("x2") = Arg2;
  register UINT64  X3 __asm__ ("x3") = 0;

  __asm__ volatile (
    "smc #0"
    : "+r" (X0), "+r" (X1), "+r" (X2), "+r" (X3)
    :
    : "memory", "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11",
      "x12", "x13", "x14", "x15", "x16", "x17"
    );
  return X0;
}

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
  //
  // 2026-11-18 device: gRT->ResetSystem(EfiResetWarm) returned at once and the
  // menu came back with no timeout, stranding the phone. Issue PSCI
  // SYSTEM_RESET2 type 0 directly, as the XNU PongBootReset does on this phone.
  //
  if (ZizhanSmc (ZIZHAN_PSCI_FEATURES, ZIZHAN_PSCI_SYSTEM_RESET2, 0) == 0) {
    (VOID)ZizhanSmc (ZIZHAN_PSCI_SYSTEM_RESET2, 0, 0);
  }

  SystemTable->RuntimeServices->ResetSystem (EfiResetWarm, EFI_SUCCESS, 0, NULL);
  //
  // Never return to the menu: let the Gunyah watchdog bite in 1 s (the
  // kernel's own fallback) and wait for it.
  //
  (VOID)ZizhanSmc (ZIZHAN_GUNYAH_WDT_CONTROL, ZIZHAN_GUNYAH_WDT_DISABLE, 0);
  (VOID)ZizhanSmc (ZIZHAN_GUNYAH_WDT_SET_TIME, 1000, 1000);
  (VOID)ZizhanSmc (ZIZHAN_GUNYAH_WDT_CONTROL, ZIZHAN_GUNYAH_WDT_ENABLE, 0);
  for ( ; ; ) {
    __asm__ volatile ("wfe" : : : "memory");
  }
}
