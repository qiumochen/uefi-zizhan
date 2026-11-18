OUTPUT_ARCH(aarch64)
ENTRY(_Head)
SECTIONS
{
  . = 0;
  .text : {
    *(.text.BootShim)
    *(.text.BootShimC)
    *(.text*)
    *(.rodata*)
    *(.data*)
  }
  _Payload = .;
  /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
