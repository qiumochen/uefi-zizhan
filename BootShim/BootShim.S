.global _Head
_Head:
  adr   x1, _Payload    /* Set _Entry address */
  b     _Start          /* Jump to the real code */

#if REQUIRES_KERNEL_HEADER == 1
_StackBase:
  .quad 0               /* Text Offset: ABL default load base */

_StackSize:
  .quad FD_SIZE + (_Payload - _Head) /* patched to the full kernel field */
  .quad 0               /* Flags */
  .quad 0               /* Reserved */
  .quad 0               /* Reserved */
  .quad 0               /* Reserved */
  .ascii "ARM\x64"      /* ARM64 Magic */
  .long 0               /* Reserved */
#endif

_Start:
  /* A' handoff: the UEFI menu writes 128 * 0x5241 at 0xA7500000 and warms
   * reset. The ABL reloads this kernel field, so the BootShim sees the magic
   * before the UEFI starts. x0 is the ABL FDT pointer and is never written
   * here; the Android branch only zeroes x1..x3. */
  ldr   x4, =0xA7500000
  mov   x6, #128
  mov   w7, #0x5241
_MagicLoop:
  ldrh  w5, [x4], #2
  cmp   w5, w7
  b.ne  _NoMagic
  subs  x6, x6, #1
  b.ne  _MagicLoop
  b     _MagicHit

_NoMagic:
  /* The UEFI path needs PONGFAT, which now lives after the FD and DTB in the
   * kernel field. Patch the ABL FDT's /chosen linux,initrd-start/end to point
   * at it before entering the existing UEFI; the Android branch never runs
   * this and therefore keeps the stock initrd. */
  mov   x19, x0                 /* ABL FDT */
  mov   x20, x1                 /* _Payload */
  mov   x21, sp
  ldr   x9, =0xA74FF000         /* scratch stack in UEFI FD Reserved */
  mov   sp, x9
  ldr   x2, _PongFatOffset
  add   x2, x20, x2             /* PONGFAT start */
  ldr   x3, _PongFatSize
  add   x3, x2, x3              /* PONGFAT end */
  mov   x0, x19
  mov   x1, x2
  mov   x2, x3
  bl    pong_fdt_set_initrd
  mov   sp, x21
  mov   x0, x19                 /* UEFI still needs the original FDT in x0 */
  mov   x1, x20
  mov   x4, x1
  ldr   x5, =FD_BASE
  cmp   x4, x5
  beq   _Entry
  ldr   x6,  =FD_SIZE

_CopyLoop:
  ldp   x2, x3, [x4], #16
  stp   x2, x3, [x5], #16
  subs  x6, x6, #16
  b.ne  _CopyLoop
  ldr   x5, =FD_BASE

_Entry:
  br    x5

_MagicHit:
  /* Clear the 256-byte magic and clean/invalidate each 64-byte line so the
   * next boot cannot see a stale match. x0 stays untouched. */
  ldr   x4, =0xA7500000
  mov   x6, #4
_ClearLine:
  mov   x8, x4
  mov   x9, #8
_ClearWords:
  str   xzr, [x8], #8
  subs  x9, x9, #1
  b.ne  _ClearWords
  dc    civac, x4
  add   x4, x4, #64
  subs  x6, x6, #1
  b.ne  _ClearLine
  dsb   sy
  mov   x1, xzr
  mov   x2, xzr
  mov   x3, xzr
  adr   x5, _Head
  ldr   x6, _AndroidOffset
  add   x5, x5, x6
  br    x5

_Dead:
  b     _Dead

.text
.align 4

/* Patched by the A' image composer (offsets from _Head / _Payload). */
.align 3
_AndroidOffset:
  .quad 0x1111111111111111
_PongFatOffset:
  .quad 0x2222222222222222
_PongFatSize:
  .quad 0x3333333333333333

.ltorg
