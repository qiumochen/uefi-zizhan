/** @file
  Show the XNU ramoops console left in DRAM by the previous boot.

  UniXNU-pong's XNU (XNU_PONG_RAMOOPS_CONSOLE) mirrors its console into a Linux
  persistent_ram buffer at 0x85200000 (console) and 0x85400000 (diagnostics).
  After a forced reset this app is fastboot-booted through the zizhan UEFI and
  pages the tail of both buffers in 5x-scaled glyphs, because the XNU text
  console font is too small for the 720p camera that watches the panel.
  Read only: the buffers are copied, never written.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/IoLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Protocol/GraphicsOutput.h>
#include "DiagFont.h"

#define RAMOOPS_BASE        0x85200000ULL
#define RAMOOPS_ZONE_BYTES  0x00200000U
#define RAMOOPS_HEADER      12U
#define RAMOOPS_SIG         0x43474244U
#define SCALE               5
#define CELL_W              (6 * SCALE)
#define CELL_H              (14 * SCALE)
#define PAGE_SECONDS        4
#define MAX_PAGES           10

STATIC CONST UINT64 mFont[] = GLYPH_FONT;
STATIC EFI_GRAPHICS_OUTPUT_PROTOCOL *mGop;
STATIC UINTN mCols, mRows;

STATIC VOID Clear (VOID) {
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL Black = { 0, 0, 0, 0 };
  mGop->Blt (mGop, &Black, EfiBltVideoFill, 0, 0, 0, 0,
             mGop->Mode->Info->HorizontalResolution, mGop->Mode->Info->VerticalResolution, 0);
}

STATIC VOID DrawLine (CONST CHAR8 *Text, UINTN Len, UINTN Row, BOOLEAN Inverse) {
  UINT32 *Fb = (UINT32 *)(UINTN)mGop->Mode->FrameBufferBase;
  UINTN  Stride = mGop->Mode->Info->PixelsPerScanLine;
  UINTN  Col, Gx, Gy, Dx, Dy, X, Y;
  UINT32 Fg = Inverse ? 0xFF000000 : 0xFFFFFFFF, Bg = Inverse ? 0xFFFFFFFF : 0xFF000000;
  UINT64 Glyph, Data;
  for (Col = 0; Col < mCols; Col++) {
    CHAR8 Ch = (Col < Len) ? Text[Col] : ' ';
    if ((Ch < 32) || (Ch > 126)) { Ch = (Ch == '\t') ? ' ' : '?'; }
    Glyph = mFont[Ch - 32];
    X = Col * CELL_W;
    Y = Row * CELL_H;
    for (Gy = 0; Gy < CELL_H; Gy++) {
      for (Gx = 0; Gx < CELL_W; Gx++) { Fb[(Y + Gy) * Stride + X + Gx] = Bg; }
    }
    for (Gy = 0; Gy < 12; Gy++) {
      Data = (Gy < 6) ? ((Glyph >> 32) >> (Gy * 5)) : ((Glyph & 0xFFFFFFFFULL) >> ((Gy - 6) * 5));
      for (Gx = 0; Gx < 5; Gx++) {
        if (((Data >> Gx) & 1) == 0) { continue; }
        for (Dy = 0; Dy < SCALE; Dy++) {
          for (Dx = 0; Dx < SCALE; Dx++) { Fb[(Y + SCALE + Gy * SCALE + Dy) * Stride + X + Gx * SCALE + Dx] = Fg; }
        }
      }
    }
  }
}

// Copy one persistent_ram zone into a linear, oldest-first buffer. Returns its length (0 if invalid).
STATIC UINTN ReadZone (UINT64 Base, CHAR8 *Out, UINT32 *Sig, UINT32 *Start, UINT32 *Size) {
  UINT32 Cap = RAMOOPS_ZONE_BYTES - RAMOOPS_HEADER, I, N = 0;
  *Sig = MmioRead32 (Base); *Start = MmioRead32 (Base + 4); *Size = MmioRead32 (Base + 8);
  if ((*Sig != RAMOOPS_SIG) || (*Size > Cap) || (*Start > Cap)) { return 0; }
  // Aligned 32-bit reads: the region is mapped non-cacheable by the firmware memory map.
  CHAR8 *Raw = AllocatePool (Cap + 4);
  if (Raw == NULL) { return 0; }
  for (I = 0; I < Cap; I += 4) { *(UINT32 *)(Raw + I) = MmioRead32 (Base + RAMOOPS_HEADER + I); }
  if (*Size < Cap) {
    CopyMem (Out, Raw, *Size); N = *Size;
  } else {
    CopyMem (Out, Raw + *Start, Cap - *Start); CopyMem (Out + Cap - *Start, Raw, *Start); N = Cap;
  }
  FreePool (Raw);
  return N;
}

// Split Text into display rows (wrapping long lines). Fills RowStart/RowLen, returns the row count.
STATIC UINTN Layout (CONST CHAR8 *Text, UINTN Len, UINTN *RowStart, UINTN *RowLen, UINTN Max) {
  UINTN Rows = 0, Pos = 0, LineEnd, Take;
  while ((Pos < Len) && (Rows < Max)) {
    for (LineEnd = Pos; (LineEnd < Len) && (Text[LineEnd] != '\n'); LineEnd++) { }
    do {
      Take = MIN (LineEnd - Pos, mCols);
      RowStart[Rows] = Pos; RowLen[Rows] = Take; Rows++; Pos += Take;
    } while ((Pos < LineEnd) && (Rows < Max));
    Pos = LineEnd + 1;
  }
  return Rows;
}

STATIC VOID ShowZone (CONST CHAR8 *Name, UINT64 Base, CHAR8 *Buf, UINTN *RowStart, UINTN *RowLen, UINTN Max) {
  UINT32 Sig, Start, Size;
  CHAR8  Title[160];
  UINTN  Len = ReadZone (Base, Buf, &Sig, &Start, &Size), Rows, Body = mRows - 1, Page, Pages, First, R;
  if (Len == 0) {
    Clear ();
    AsciiSPrint (Title, sizeof Title, "%a @%lx: no ramoops sig=%08x start=%x size=%x", Name, Base, Sig, Start, Size);
    DrawLine (Title, AsciiStrLen (Title), 0, TRUE);
    gBS->Stall (PAGE_SECONDS * 1000000);
    return;
  }
  Rows = Layout (Buf, Len, RowStart, RowLen, Max);
  Pages = MIN ((Rows + Body - 1) / Body, MAX_PAGES);
  for (Page = 0; Page < Pages; Page++) {
    Clear ();
    AsciiSPrint (Title, sizeof Title, "%a bytes=%u rows=%u page -%u/%u (newest first)", Name, Len, Rows, Page, Pages);
    DrawLine (Title, AsciiStrLen (Title), 0, TRUE);
    First = (Rows > (Page + 1) * Body) ? Rows - (Page + 1) * Body : 0;
    for (R = 0; (R < Body) && (First + R < Rows - Page * Body); R++) {
      DrawLine (Buf + RowStart[First + R], RowLen[First + R], R + 1, FALSE);
    }
    gBS->Stall (PAGE_SECONDS * 1000000);
  }
}

EFI_STATUS EFIAPI RamoopsViewEntry (EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
  UINTN  Max = 200000, Loop;
  CHAR8  *Buf;
  UINTN  *RowStart, *RowLen;
  if (EFI_ERROR (gBS->LocateProtocol (&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **)&mGop))) { return EFI_UNSUPPORTED; }
  // The camera crops the right quarter of the panel; keep rows inside its view.
  mCols = MIN (mGop->Mode->Info->HorizontalResolution / CELL_W, 54);
  mRows = mGop->Mode->Info->VerticalResolution / CELL_H;
  Buf = AllocatePool (RAMOOPS_ZONE_BYTES);
  RowStart = AllocatePool (Max * sizeof (UINTN));
  RowLen = AllocatePool (Max * sizeof (UINTN));
  if ((Buf == NULL) || (RowStart == NULL) || (RowLen == NULL)) { return EFI_OUT_OF_RESOURCES; }
  gBS->SetWatchdogTimer (0, 0, 0, NULL);
  for (Loop = 0; Loop < 3; Loop++) {
    ShowZone ("CONSOLE", RAMOOPS_BASE, Buf, RowStart, RowLen, Max);
    ShowZone ("DIAG", RAMOOPS_BASE + RAMOOPS_ZONE_BYTES, Buf, RowStart, RowLen, Max);
  }
  gRT->ResetSystem (EfiResetCold, EFI_SUCCESS, 0, NULL);
  return EFI_SUCCESS;
}
