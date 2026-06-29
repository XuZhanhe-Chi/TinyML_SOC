ENTRY(_start)

MEMORY
{
  SRAM (rwx) : ORIGIN = 0x00000000, LENGTH = 64K
}

SECTIONS
{
  .text : {
    KEEP(*(.text._start))
    *(.text*)
  } > SRAM
}
