/** @file
  Save the XNU ramoops console left in DRAM by the previous boot into the
  `logfs` FAT partition as \XNULOG.TXT, then cold-reset into Android, where the
  host reads the partition back over adb. This replaces photographing
  RamoopsView pages: the whole log arrives as text.

  logfs is the 8 MiB FAT volume the stock Qualcomm UEFI already writes its
  UefiLog0..4.txt into on every boot; only this one file is replaced. The
  partition is located by its GPT name through EFI_PARTITION_INFO_PROTOCOL.
  The ramoops buffers are only read, never written.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include <Uefi.h>
#include <Guid/FileInfo.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/IoLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Protocol/PartitionInfo.h>
#include <Protocol/SimpleFileSystem.h>

#define RAMOOPS_BASE        0x85200000ULL
#define RAMOOPS_ZONE_BYTES  0x00200000U
#define RAMOOPS_HEADER      12U
#define RAMOOPS_SIG         0x43474244U

// Append one persistent_ram zone to Out, oldest first. Returns the bytes appended.
STATIC UINTN AppendZone (CONST CHAR8 *Name, UINT64 Base, CHAR8 *Out) {
  UINT32 Cap = RAMOOPS_ZONE_BYTES - RAMOOPS_HEADER, I;
  UINT32 Sig = MmioRead32 (Base), Start = MmioRead32 (Base + 4), Size = MmioRead32 (Base + 8);
  UINTN  N = AsciiSPrint (Out, 160, "\n===== %a @%lx sig=%08x start=%x size=%x =====\n", Name, Base, Sig, Start, Size);
  CHAR8  *Raw;
  if ((Sig != RAMOOPS_SIG) || (Size > Cap) || (Start > Cap)) { return N; }
  Raw = AllocatePool (Cap + 4);
  if (Raw == NULL) { return N; }
  // Aligned 32-bit reads: the region is mapped non-cacheable by the firmware memory map.
  for (I = 0; I < Cap; I += 4) { *(UINT32 *)(Raw + I) = MmioRead32 (Base + RAMOOPS_HEADER + I); }
  if (Size < Cap) {
    CopyMem (Out + N, Raw, Size); N += Size;
  } else {
    CopyMem (Out + N, Raw + Start, Cap - Start); CopyMem (Out + N + Cap - Start, Raw, Start); N += Cap;
  }
  FreePool (Raw);
  return N;
}

STATIC EFI_FILE_PROTOCOL *OpenLogfsRoot (VOID) {
  EFI_HANDLE                      *Handles;
  UINTN                           Count, I;
  EFI_PARTITION_INFO_PROTOCOL     *Info;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Fs;
  EFI_FILE_PROTOCOL               *Root = NULL;

  // Make sure every UFS LUN, partition and FAT volume is connected.
  if (!EFI_ERROR (gBS->LocateHandleBuffer (AllHandles, NULL, NULL, &Count, &Handles))) {
    for (I = 0; I < Count; I++) { gBS->ConnectController (Handles[I], NULL, NULL, TRUE); }
    FreePool (Handles);
  }
  if (EFI_ERROR (gBS->LocateHandleBuffer (ByProtocol, &gEfiPartitionInfoProtocolGuid, NULL, &Count, &Handles))) {
    return NULL;
  }
  for (I = 0; (I < Count) && (Root == NULL); I++) {
    if (EFI_ERROR (gBS->HandleProtocol (Handles[I], &gEfiPartitionInfoProtocolGuid, (VOID **)&Info)) ||
        (Info->Type != PARTITION_TYPE_GPT) || (StrCmp (Info->Info.Gpt.PartitionName, L"logfs") != 0)) {
      continue;
    }
    if (!EFI_ERROR (gBS->HandleProtocol (Handles[I], &gEfiSimpleFileSystemProtocolGuid, (VOID **)&Fs))) {
      Fs->OpenVolume (Fs, &Root);
    }
  }
  FreePool (Handles);
  return Root;
}

EFI_STATUS EFIAPI RamoopsDumpEntry (EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
  CHAR8             *Buf;
  UINTN             Len, Written;
  EFI_FILE_PROTOCOL *Root, *File;
  EFI_STATUS        Status = EFI_NOT_FOUND;

  gBS->SetWatchdogTimer (0, 0, 0, NULL);
  Print (L"RamoopsDump: saving ramoops to logfs:\\XNULOG.TXT\n");
  Buf = AllocatePool (2 * RAMOOPS_ZONE_BYTES + 4096);
  if (Buf == NULL) { return EFI_OUT_OF_RESOURCES; }
  Len = AsciiSPrint (Buf, 128, "XNULOG v1 (zizhan RamoopsDump)\n");
  Len += AppendZone ("CONSOLE", RAMOOPS_BASE, Buf + Len);
  Len += AppendZone ("DIAG", RAMOOPS_BASE + RAMOOPS_ZONE_BYTES, Buf + Len);

  Root = OpenLogfsRoot ();
  if (Root != NULL) {
    // Replace any previous dump: open, delete, recreate.
    if (!EFI_ERROR (Root->Open (Root, &File, L"XNULOG.TXT", EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0))) {
      File->Delete (File);
    }
    Status = Root->Open (Root, &File, L"XNULOG.TXT", EFI_FILE_MODE_CREATE | EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0);
    if (!EFI_ERROR (Status)) {
      Written = Len;
      Status = File->Write (File, &Written, Buf);
      File->Flush (File);
      File->Close (File);
    }
    Root->Close (Root);
  }
  Print (L"RamoopsDump: %u bytes, status %r; resetting\n", Len, Status);
  gBS->Stall (EFI_ERROR (Status) ? 30000000 : 1000000);
  gRT->ResetSystem (EfiResetCold, EFI_SUCCESS, 0, NULL);
  return Status;
}
